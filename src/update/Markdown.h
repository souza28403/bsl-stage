#pragma once

#include <string>
#include <string_view>
#include <vector>

namespace bsl::update {

enum class MdKind { Paragraph, Heading1, Heading2, Heading3, Bullet, Numbered, Quote, Code, Rule };

struct MdBlock
{
    MdKind kind = MdKind::Paragraph;
    std::string text;
    std::string marker;
    int indent = 0;
};

std::vector<MdBlock> parseMarkdown(std::string_view markdown);

std::string stripInlineMarkdown(std::string_view line);

}
