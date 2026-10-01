#pragma once

#include <format>
#include <ranges>
#include <regex>
#include <set>
#include <span>
#include <string>
#include <string_view>
#include <tuple>
#include <vector>

#include <anitomy/detail/container.hpp>
#include <anitomy/detail/element.hpp>
#include <anitomy/detail/token.hpp>
#include <anitomy/detail/util.hpp>
#include <anitomy/element.hpp>

namespace anitomy::detail {

inline std::vector<Element> parse_episode_numbers(std::span<Token> tokens) noexcept {
  using namespace std::views;

  std::vector<Element> elements;

  const auto add_element_from_token = [&elements](ElementKind kind, Token& token,
                                                  std::string_view value = {},
                                                  size_t position = std::string::npos) {
    token.element_kind = kind;
    elements.emplace_back(element_from_token(kind, token, value, position));
  };

  {
    static constexpr auto match_episode_token = [](const Token& token, std::smatch& matches) {
      static const std::regex pattern{
          "(?:S(\\d{1,2})|(\\d{1,2})x)?"  // season
          "[E#]?(\\d{1,4})"               // episode
          "(?:[vV](\\d))?"                // version
      };
      return std::regex_match(token.value, matches, pattern);
    };

    static constexpr auto parse_matches = [](const std::smatch& matches, Token& token,
                                             std::vector<Element>& elements) {
      if (matches[1].matched) {
        elements.emplace_back(ElementKind::Season, matches.str(1),
                              token.position + matches.position(1));
      } else if (matches[2].matched) {
        elements.emplace_back(ElementKind::Season, matches.str(2),
                              token.position + matches.position(2));
      }

      token.element_kind = ElementKind::Episode;
      elements.emplace_back(element_from_token(ElementKind::Episode, token, matches.str(3),
                                               token.position + matches.position(3)));

      if (matches[4].matched) {
        elements.emplace_back(ElementKind::ReleaseVersion, matches.str(4),
                              token.position + matches.position(4));
      }
    };

    static constexpr auto is_episode_keyword = [](const Token& token) {
      return token.keyword && token.keyword->kind == KeywordKind::Episode;
    };

    static constexpr auto is_type_keyword = [](const Token& token) {
      return token.keyword && (token.keyword->kind == KeywordKind::Type ||
                               token.keyword->kind == KeywordKind::EpisodeType);
    };

    static constexpr auto is_episode_delimiter = [](const Token& token) {
      static const std::set<char> delimiters{'-', '~', '&', '+'};
      return is_delimiter_token(token) && delimiters.contains(token.value.front());
    };

    static constexpr auto starts_with_episode_or_type_keyword = [](std::span<Token>& tokens,
                                                                   std::span<Token>::iterator& it) {
      if (it == tokens.end()) return false;
      if (is_episode_keyword(*it)) return true;
      if (is_type_keyword(*it)) return it->value != "Movie";
      return false;
    };

    static constexpr auto is_episode_range = [](std::span<Token>& tokens,
                                                std::span<Token>::iterator& it,
                                                std::pair<std::smatch, std::smatch>& matches) {
      if (it == tokens.end()) return false;
      if (!is_episode_delimiter(*it)) return false;
      if (++it == tokens.end()) return false;
      if (!match_episode_token(*it, matches.second)) return false;
      if (to_int(matches.first.str(3)) >= to_int(matches.second.str(3))) {
        return false;  // avoid matching `009-1`, `5-2`, etc.
      }
      return true;
    };

    auto view = tokens | filter(is_free_token);

    for (auto token = view.begin(); token != view.end(); ++token) {
      std::pair<std::smatch, std::smatch> matches;

      if (!match_episode_token(*token, matches.first)) continue;

      auto prev_token = find_prev_token(tokens, token.base(), is_not_delimiter_token);
      auto next_token = std::next(token.base());

      // A numeric token (e.g. `1`) is valid only if preceded by a keyword
      // (e.g. `Episode 1`, `OVA1`) or is part of a range (e.g. `1-12`).
      bool valid = !is_numeric_token(*token);
      valid = starts_with_episode_or_type_keyword(tokens, prev_token) || valid;
      valid = is_episode_range(tokens, next_token, matches) || valid;
      if (!valid) continue;

      parse_matches(matches.first, *token, elements);
      if (!matches.second.empty()) {
        parse_matches(matches.second, *next_token, elements);
      }
    }

    if (!elements.empty()) return elements;
  }

  // Separated episodes (e.g. `8 & 10`, `1 ~ 12`, `01 of 24`)
  {
    static constexpr auto is_separator = [](const Token& token) {
      return token.value == "&" || token.value == "~" || token.value == "of";
    };

    auto view = tokens | filter(is_free_token) | filter(is_numeric_token);

    for (auto it = view.begin(); it != view.end(); ++it) {
      auto next_token = std::next(it.base().base());
      auto token = std::ranges::find_if(next_token, tokens.end(), is_separator);
      if (token == tokens.end()) continue;
      if (std::ranges::any_of(next_token, token, is_not_delimiter_token)) continue;

      next_token = find_next_token(tokens, token, is_not_delimiter_token);
      if (next_token == tokens.end()) continue;
      if (!is_numeric_token(*next_token)) continue;

      add_element_from_token(ElementKind::Episode, *it);
      if (token->value != "of") {
        add_element_from_token(ElementKind::Episode, *next_token);
      }
      return elements;
    }
  }

  // Fractional episode (e.g. `07.5`)
  {
    for (auto [number, delimiter, fraction] : tokens | adjacent<3>) {
      if (is_free_token(number) && is_numeric_token(number)) {
        if (is_delimiter_token(delimiter) && delimiter.value == ".") {
          // We don't allow any fractional part other than `.5`, because there are cases
          // where such a number is a part of the title (e.g. `Evangelion: 1.11`,
          // `Tokyo Magnitude 8.0`) or a keyword (e.g. `5.1`).
          if (is_free_token(fraction) && fraction.value == "5") {
            add_element_from_token(
                ElementKind::Episode, number,
                std::format("{}{}{}", number.value, delimiter.value, fraction.value));
            delimiter.element_kind = ElementKind::Episode;
            fraction.element_kind = ElementKind::Episode;
            return elements;
          }
        }
      }
    }
  }

  // Japanese counter (e.g. `第01話`)
  {
    static constexpr auto match_japanese_counter = [](const Token& token, std::smatch& matches) {
      static const std::regex pattern{"(?:第)?(\\d{1,4})話"};
      return std::regex_match(token.value, matches, pattern);
    };

    std::smatch matches;

    for (auto& token : tokens | filter(is_free_token)) {
      if (match_japanese_counter(token, matches)) {
        add_element_from_token(ElementKind::Episode, token, matches.str(1),
                               token.position + matches.position(1));
        return elements;
      }
    }
  }

  // Separated number (e.g. ` - 08`)
  {
    auto view = tokens | reverse | filter(is_dash_token);

    for (auto it = view.begin(); it != view.end(); ++it) {
      auto next_token =
          std::ranges::find_if(it.base().base(), tokens.end(), is_not_delimiter_token);
      if (next_token != tokens.end() && is_free_token(*next_token) &&
          is_numeric_token(*next_token)) {
        add_element_from_token(ElementKind::Episode, *next_token);
        return elements;
      }
    }
  }

  // Isolated number (e.g. `[12]`, `(2006)`)
  {
    using window_t = std::tuple<Token&, Token&, Token&>;

    static constexpr auto is_isolated = [](window_t tokens) {
      return std::get<0>(tokens).kind == TokenKind::OpenBracket &&
             std::get<2>(tokens).kind == TokenKind::CloseBracket;
    };

    static constexpr auto is_free_number = [](window_t tokens) {
      auto& token = std::get<1>(tokens);
      return is_free_token(token) && is_numeric_token(token);
    };

    auto view = tokens | adjacent<3> | filter(is_isolated) | filter(is_free_number) | take(1);

    if (!view.empty()) {
      add_element_from_token(ElementKind::Episode, std::get<1>(view.front()));
      return elements;
    }
  }

  // Partial episode (e.g. `4a`, `111C`)
  {
    static constexpr auto is_partial_episode = [](const Token& token) {
      static const std::regex pattern{R"(\d{1,4}[ABCabc])"};
      return std::regex_match(token.value, pattern);
    };

    static constexpr auto _ = [](auto pred) {
      return [pred](const auto& tuple) { return pred(std::get<1>(tuple)); };
    };

    auto view = tokens | enumerate | filter(_(is_free_token)) | filter(_(is_partial_episode));

    for (auto [i, token] : view) {
      if (i > 1 && tokens[i - 2].value == "Ver1" && token.value == "1a") {
        continue;  // `NieR:Automata Ver1.1a`
      }

      add_element_from_token(ElementKind::Episode, token);
      return elements;
    }
  }

  // First number
  {
    static constexpr auto starts_with_episode_number = [](std::span<Token>& tokens) {
      if (tokens.empty()) return false;

      const auto& token = tokens.front();
      if (!is_free_token(token) || !is_numeric_token(token)) return false;

      if (tokens.size() <= 2) return true;
      if (is_dash_token(tokens[1]) || is_dash_token(tokens[2])) return true;
      if (tokens[1].value == ".") {
        if (is_space(tokens[2].value.front())) return true;
        if (tokens[2].element_kind == ElementKind::FileExtension) return true;
      }

      return false;
    };

    if (starts_with_episode_number(tokens)) {
      add_element_from_token(ElementKind::Episode, tokens.front());
      return elements;
    }
  }

  // Last number
  {
    static constexpr auto is_version_number = [](auto token) {
      if (!is_numeric_token(*token)) return false;
      const auto it = std::prev(token);
      return is_delimiter_token(*it) && it->value == ".";  // e.g. `1.1`, `3.33`
    };

    auto view = tokens | reverse | filter(is_free_token) | filter(is_numeric_token);

    for (auto token = view.begin(); token != view.end(); ++token) {
      if (token->is_enclosed) continue;
      if (token->position == 0) continue;

      const auto prev_token =
          find_next_token(token.base().base(), tokens.rend(), is_not_delimiter_token);
      const auto next_token =
          find_next_token(token.base().base().base(), tokens.end(), is_not_delimiter_token);

      if (prev_token != tokens.rend()) {
        auto& value = prev_token->value;
        if (equal(value, "Cour") || equal(value, "Part")) continue;  // e.g. `Cour 2`, `Part 2`
        if (equal(value, "Movie")) continue;                         // e.g. `Movie 9`
        if (equal(value, "No")) continue;                            // e.g. `No.6`
        if (is_version_number(prev_token)) continue;
        if (is_close_bracket_token(*prev_token) && value == "]") continue;
      }
      if (next_token != tokens.end()) {
        if (is_version_number(next_token)) continue;
      }
      if (prev_token != tokens.rend() && next_token != tokens.end()) {
        if (is_free_token(*prev_token) && is_free_token(*next_token)) continue;
      }

      add_element_from_token(ElementKind::Episode, *token);
      return elements;
    }
  }

  return elements;
}

inline std::vector<Element> parse_episode(std::span<Token> tokens) noexcept {
  auto elements = parse_episode_numbers(tokens);

  const auto is_available_number = [](const Token& token) {
    if (!is_free_token(token) && token.element_kind != ElementKind::Episode) return false;
    if (is_numeric_token(token)) return token.value.size() <= 4;
    static const std::regex versioned_number{R"(\d{1,4}[vV]\d)"};
    return token.element_kind == ElementKind::Episode &&
           std::regex_match(token.value, versioned_number);
  };
  const auto is_separator = [](const Token& token) {
    return is_delimiter_token(token) &&
           (is_dash_token(token) || is_space(token.value.front()) || token.value == "_");
  };
  const auto next_content = [&](size_t index) {
    while (index < tokens.size() && is_separator(tokens[index])) ++index;
    return index;
  };
  const auto previous_content = [&](size_t index) {
    while (index > 0 && is_separator(tokens[index - 1])) --index;
    return index == 0 ? tokens.size() : index - 1;
  };
  const auto has_release_dash = [&](size_t index) {
    while (index > 0 && is_delimiter_token(tokens[index - 1]) &&
           (is_space(tokens[index - 1].value.front()) || tokens[index - 1].value == "_")) --index;
    return index > 0 && is_dash_token(tokens[index - 1]);
  };
  const auto episode_element = [&](size_t index) {
    const auto& token = tokens[index];
    return std::ranges::find_if(elements, [&](const Element& element) {
      return element.kind == ElementKind::Episode && element.position >= token.position &&
             element.position < token.position + token.value.size();
    });
  };
  const auto number_value = [&](size_t index) {
    const auto element = episode_element(index);
    return element == elements.end() ? tokens[index].value : element->value;
  };
  const auto add_alternative = [&](size_t index, std::string value = {}) {
    auto& token = tokens[index];
    auto element = episode_element(index);
    if (element != elements.end()) {
      element->kind = ElementKind::EpisodeAlternative;
      if (!value.empty()) element->value = std::move(value);
    } else {
      elements.emplace_back(element_from_token(ElementKind::EpisodeAlternative, token, value));
    }
    token.element_kind = ElementKind::EpisodeAlternative;
  };
  const auto enclosed_number = [&](size_t index) -> std::optional<size_t> {
    index = next_content(index);
    if (index == tokens.size()) return {};
    const auto& open = tokens[index];
    if (open.value != "(") return {};
    const auto number = next_content(index + 1);
    if (number == tokens.size() || !is_available_number(tokens[number])) return {};
    const auto close = next_content(number + 1);
    if (close == tokens.size() || tokens[close].value != ")") {
      return {};
    }
    return number;
  };

  // Explicit season/episode tags take precedence over alternate numbering.
  static const std::regex explicit_pattern{R"((?:S\d{1,2}E|\d{1,2}x)\d{1,4}(?:[vV]\d)?)"};
  for (size_t index = 0; index < tokens.size(); ++index) {
    if (tokens[index].element_kind != ElementKind::Episode ||
        !std::regex_match(tokens[index].value, explicit_pattern)) continue;

    const auto add_for_primary = [&](size_t number, std::string value = {}) {
      if (value.empty()) value = number_value(number);
      const auto primary = episode_element(index);
      if (primary != elements.end() && !value.contains('.') &&
          to_int(primary->value) == to_int(value)) {
        const auto position = tokens[number].position;
        std::erase_if(elements, [&](const Element& element) {
          return element.kind == ElementKind::Episode && element.position == position;
        });
        tokens[number].element_kind = ElementKind::EpisodeAlternative;
      } else {
        add_alternative(number, value);
      }
    };

    // Absolute number before the tag: `46 (S04E10)` or `18.5 - S00E05`.
    auto previous = previous_content(index);
    if (previous != tokens.size() && tokens[previous].value == "(") {
      const auto close = next_content(index + 1);
      if (close < tokens.size() && tokens[close].value == ")") {
        previous = previous_content(previous);
      }
    }
    if (previous != tokens.size() && is_available_number(tokens[previous])) {
      auto first = previous;
      std::string value = number_value(previous);
      if (previous >= 2 && tokens[previous].value == "5" &&
          tokens[previous - 1].value == "." && is_available_number(tokens[previous - 2])) {
        first = previous - 2;
        value = tokens[first].value + ".5";
      }
      if (has_release_dash(first)) {
        add_for_primary(first, value);
        for (auto part = first + 1; part <= previous; ++part) {
          tokens[part].element_kind = ElementKind::EpisodeAlternative;
        }
      }
    }

    // Absolute number immediately after the tag: `S04E10 (46)`.
    auto after = next_content(index + 1);
    if (after < tokens.size() && tokens[after].value == ")" &&
        index > 0 && tokens[index - 1].value == "(") {
      after = next_content(after + 1);
    }
    if (auto number = enclosed_number(after)) add_for_primary(*number);

    // A later enclosed `Episode 83` is alternate numbering, not another range member.
    for (size_t number = index + 1; number < tokens.size(); ++number) {
      if (!tokens[number].is_enclosed || !is_available_number(tokens[number]) ||
          tokens[number].element_kind != ElementKind::Episode) continue;
      const auto prefix = previous_content(number);
      if (prefix == tokens.size() || !tokens[prefix].keyword ||
          tokens[prefix].keyword->kind != KeywordKind::Episode) continue;
      auto primary = episode_element(index);
      if (primary == elements.end()) continue;
      if (to_int(primary->value) == to_int(tokens[number].value)) {
        primary->value = tokens[number].value;
        std::erase_if(elements, [&](const Element& element) {
          return element.kind == ElementKind::Episode && element.position == tokens[number].position;
        });
        tokens[number].element_kind = ElementKind::EpisodeAlternative;
      } else {
        add_alternative(number);
      }
    }
  }

  // Without an explicit tag, the smaller of adjacent `01 (176)` numbers is local.
  for (size_t index = 0; index < tokens.size(); ++index) {
    if (tokens[index].is_enclosed || !is_available_number(tokens[index]) ||
        !has_release_dash(index)) continue;
    const auto number = enclosed_number(index + 1);
    if (!number) continue;
    const auto local = to_int(tokens[index].value) <= to_int(tokens[*number].value) ? index : *number;
    const auto alternative = local == index ? *number : index;
    if (to_int(tokens[local].value) == to_int(tokens[alternative].value)) continue;
    add_alternative(alternative);
    if (episode_element(local) == elements.end()) {
      elements.emplace_back(element_from_token(ElementKind::Episode, tokens[local]));
    }
    tokens[local].element_kind = ElementKind::Episode;
  }

  return elements;
}

}  // namespace anitomy::detail
