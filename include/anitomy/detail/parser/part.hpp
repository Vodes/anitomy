#pragma once

#include <ranges>
#include <regex>
#include <span>

#include <anitomy/detail/token.hpp>
#include <anitomy/detail/util.hpp>
#include <anitomy/element.hpp>

namespace anitomy::detail {

inline std::optional<Element> parse_part(std::span<Token> tokens) noexcept {
  static constexpr auto is_part_keyword = [](const Token& token) {
    return token.keyword && token.keyword->kind == KeywordKind::Part;
  };

  auto view = tokens | std::views::filter(is_part_keyword);

  for (auto it = view.begin(); it != view.end(); ++it) {
    // Ordinal before the keyword (e.g. `2nd Cour`).
    auto prev_token = find_prev_token(tokens, it.base(), is_not_delimiter_token);
    if (prev_token != tokens.end() && is_free_token(*prev_token)) {
      if (auto number = from_ordinal_number(prev_token->value); !number.empty()) {
        it->element_kind = ElementKind::Part;
        prev_token->element_kind = ElementKind::Part;
        return element_from_token(ElementKind::Part, *prev_token, number);
      }
    }

    auto next_token = find_next_token(tokens, it.base(), is_not_delimiter_token);

    if (next_token == tokens.end()) continue;
    if (!is_numeric_token(*next_token)) continue;

    it->element_kind = ElementKind::Part;
    next_token->element_kind = ElementKind::Part;

    return element_from_token(ElementKind::Part, *next_token);
  }

  // Abbreviated part (e.g. `S3 P2`).
  static const std::regex pattern{R"(P(\d{1,2}))"};
  std::smatch matches;
  for (auto& token : tokens | std::views::filter(is_free_token)) {
    if (std::regex_match(token.value, matches, pattern)) {
      token.element_kind = ElementKind::Part;
      return element_from_token(ElementKind::Part, token, matches.str(1),
                               token.position + matches.position(1));
    }
  }

  return {};
}

}  // namespace anitomy::detail
