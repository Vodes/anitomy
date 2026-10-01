#pragma once

#include <ranges>
#include <regex>
#include <span>
#include <string>
#include <vector>

#include <anitomy/detail/token.hpp>
#include <anitomy/element.hpp>
#include <anitomy/options.hpp>

namespace anitomy::detail {

inline std::vector<Element> parse_keywords(std::span<Token> tokens,
                                           const Options& options) noexcept {
  using enum KeywordKind;
  using namespace std::views;

  static constexpr auto to_element_kind = [](const KeywordKind kind) {
    using E = ElementKind;
    // clang-format off
    switch (kind) {
      case AudioChannels:      return E::AudioTerm;
      case AudioCodec:         return E::AudioTerm;
      case AudioLanguage:      return E::AudioTerm;
      case Device:             return E::Device;
      case Episode:            return E::Episode;
      case EpisodeType:        return E::Type;
      case Language:           return E::Language;
      case Other:              return E::Other;
      case Part:               return E::Part;
      case ReleaseGroup:       return E::ReleaseGroup;
      case ReleaseInformation: return E::ReleaseInformation;
      case ReleaseVersion:     return E::ReleaseVersion;
      case Season:             return E::Season;
      case Source:             return E::Source;
      case Subtitles:          return E::Subtitles;
      case SubtitleLanguage:   return E::Subtitles;
      case Type:               return E::Type;
      case VideoCodec:         return E::VideoTerm;
      case VideoColorDepth:    return E::VideoTerm;
      case VideoDynamicRange:  return E::VideoTerm;
      case VideoFormat:        return E::VideoTerm;
      case VideoFrameRate:     return E::VideoTerm;
      case VideoProfile:       return E::VideoTerm;
      case VideoQuality:       return E::VideoTerm;
      case VideoResolution:    return E::VideoResolution;
      case Volume:             return E::Volume;
    }
    // clang-format on
    return E::Other;
  };

  static constexpr auto is_prefix = [](const KeywordKind kind) {
    return kind == Episode || kind == Part || kind == Season || kind == Volume;
  };

  const auto is_allowed = [&options](const Token& token) {
    if (!token.keyword) return false;
    if (token.keyword->kind == ReleaseGroup) return options.parse_release_group;
    if (token.keyword->kind == VideoResolution) return options.parse_video_resolution;
    return true;
  };

  static constexpr auto token_value = [](const Token& token) -> std::string {
    if (token.keyword->kind == KeywordKind::ReleaseVersion) {
      return token.value.substr(1);  // `v2` -> `2`
    }
    return token.value;
  };

  std::vector<Element> elements;

  for (auto [index, token] : tokens | enumerate) {
    if (!is_keyword_token(token) || !is_allowed(token)) continue;
    // `Episode` can also be a word in a title, without a following number.
    if (token.keyword->kind == Episode) {
      auto next_token = find_next_token(tokens, tokens.begin() + index, is_not_delimiter_token);
      static const std::regex episode_pattern{R"(\d{1,4}(?:[vV]\d)?)"};
      if (next_token == tokens.end() ||
          !std::regex_match(next_token->value, episode_pattern)) continue;
    }
    const auto element_kind = to_element_kind(token.keyword->kind);
    if (!token.keyword->is_ambiguous() || token.is_enclosed) {
      token.element_kind = element_kind;
    }
    if (!is_prefix(token.keyword->kind)) {
      elements.emplace_back(element_kind, token_value(token), token.position);
    }
  }

  return elements;
}

}  // namespace anitomy::detail
