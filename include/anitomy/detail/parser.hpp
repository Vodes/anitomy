#pragma once

#include <algorithm>
#include <span>
#include <utility>
#include <vector>

#include <anitomy/detail/parser/episode.hpp>
#include <anitomy/detail/parser/episode_title.hpp>
#include <anitomy/detail/parser/file_checksum.hpp>
#include <anitomy/detail/parser/file_extension.hpp>
#include <anitomy/detail/parser/keywords.hpp>
#include <anitomy/detail/parser/part.hpp>
#include <anitomy/detail/parser/release_group.hpp>
#include <anitomy/detail/parser/season.hpp>
#include <anitomy/detail/parser/title.hpp>
#include <anitomy/detail/parser/video_resolution.hpp>
#include <anitomy/detail/parser/volume.hpp>
#include <anitomy/detail/parser/year.hpp>
#include <anitomy/detail/token.hpp>
#include <anitomy/element.hpp>
#include <anitomy/options.hpp>

namespace anitomy::detail {

class Parser final {
public:
  explicit Parser(std::vector<Token>& tokens) : tokens_{std::move(tokens)} {
  }

  [[nodiscard]] constexpr auto&& elements(this auto&& self) noexcept {
    return std::forward<decltype(self)>(self).elements_;
  }

  [[nodiscard]] constexpr auto&& tokens(this auto&& self) noexcept {
    return std::forward<decltype(self)>(self).tokens_;
  }

  inline void parse(const Options& options) noexcept {
    auto tokens = std::span{tokens_};
    // Torrent titles may append aliases after the release metadata.
    for (auto it = tokens.begin(); it != tokens.end(); ++it) {
      if (it->value != "|" || it->is_enclosed) continue;
      const bool has_release_metadata = std::any_of(tokens.begin(), it, [](const Token& token) {
        if (!token.keyword) return false;
        return token.keyword->kind == KeywordKind::Source ||
               token.keyword->kind == KeywordKind::VideoCodec ||
               token.keyword->kind == KeywordKind::VideoResolution;
      });
      if (has_release_metadata) {
        tokens = {tokens.begin(), it};
        break;
      }
    }

    // File extension
    if (options.parse_file_extension) {
      add_element(parse_file_extension(tokens));
    }

    // Keywords
    add_elements(parse_keywords(tokens, options));

    // Checksum
    if (options.parse_file_checksum) {
      add_element(parse_file_checksum(tokens));
    }

    // Video resolution
    if (options.parse_video_resolution) {
      add_elements(parse_video_resolution(tokens));
    }

    // Year
    if (options.parse_year) {
      add_element(parse_year(tokens));
    }

    // Season
    if (options.parse_season) {
      add_elements(parse_season(tokens));
    }

    // Part
    if (options.parse_part) {
      add_element(parse_part(tokens));
    }

    // Episode
    if (options.parse_episode) {
      add_elements(parse_volume(tokens));
      add_elements(parse_episode(tokens));
    }

    // Title
    if (options.parse_title) {
      add_element(parse_title(tokens));
    }

    // Release group
    if (options.parse_release_group && !contains(ElementKind::ReleaseGroup)) {
      add_element(parse_release_group(tokens));
    }

    // Episode title
    if (options.parse_episode_title && contains(ElementKind::Episode)) {
      add_element(parse_episode_title(tokens));
    }

    std::ranges::sort(elements_, {}, &Element::position);
  }

private:
  constexpr void add_element(std::optional<Element>&& element) noexcept {
    if (element) {
      elements_.emplace_back(*element);
    }
  }

  constexpr void add_elements(std::vector<Element>&& elements) noexcept {
    if (!elements.empty()) {
      std::ranges::move(elements, std::back_inserter(elements_));
    }
  }

  [[nodiscard]] bool contains(ElementKind kind) const noexcept {
    const auto is_kind = [&kind](const Element& element) { return element.kind == kind; };
    return std::ranges::any_of(elements_, is_kind);
  }

  std::vector<Element> elements_;
  std::vector<Token> tokens_;
};

}  // namespace anitomy::detail
