/* ReaPack: Package manager for REAPER
 * Copyright (C) 2015-2019  Christian Fillion
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "filter.hpp"

#include <boost/algorithm/string/case_conv.hpp>

Filter::Filter(const std::string &input)
  : m_root(Group::MatchAll)
{
  set(input);
}

void Filter::set(const std::string &input)
{
  enum State { Default, DoubleQuote, SingleQuote };

  m_input = input;
  m_root.clear();

  std::string buf;
  int flags = 0;
  State state = Default;
  Group *group = &m_root;

  for(const char c : input) {
    if(c == '"' && state != SingleQuote) {
      state = state == Default ? DoubleQuote : Default;
      flags |= Node::QuotedFlag;
      continue;
    }
    else if(c == '\'' && state != DoubleQuote) {
      state = state == Default ? SingleQuote : Default;
      flags |= Node::QuotedFlag;
      continue;
    }
    else if(c == '\x20') {
      if(state == Default) {
        group = group->push(buf, &flags);
        buf.clear();
        continue;
      }
      else
        flags |= Node::PhraseFlag;
    }

    buf += c;
  }

  group->push(buf, &flags);
}

bool Filter::match(std::vector<std::string> rows) const
{
  for(std::string &str : rows)
    boost::algorithm::to_lower(str);

  return m_root.match(rows);
}

Filter::Group::Group(Type type, int flags, Group *parent)
  : Node(flags), m_parent(parent), m_type(type), m_open(true)
{
}

Filter::Group *Filter::Group::push(std::string buf, int *flags)
{
  if(buf.empty())
    return this;

  if((*flags & QuotedFlag) == 0) {
    if(buf == "NOT") {
      *flags ^= Token::NotFlag;
      return this;
    }
    else if(buf == "OR") {
      if(m_nodes.empty())
        return this;
      else if(m_type == MatchAny) {
        m_open = true;
        return this;
      }

      auto prev = std::move(m_nodes.back());
      m_nodes.pop_back();

      Group *newGroup = addSubGroup(MatchAny, 0);
      newGroup->m_nodes.push_back(std::move(prev));
      return newGroup;
    }
    else if(buf == "(") {
      Group *newGroup = addSubGroup(MatchAll, *flags);
      *flags = 0;
      return newGroup;
    }
    else if(buf == ")") {
      for(Group *parent = this; parent->m_parent; parent = parent->m_parent) {
        if(parent->m_type == MatchAll)
          return parent->m_parent;
      }

      return this;
    }
  }

  if(buf.size() > 1 && buf.front() == '^') {
    *flags |= Node::StartAnchorFlag;
    buf.erase(0, 1); // we need to recheck the size() below, for '$'
  }
  if(buf.size() > 1 && buf.back() == '$') {
    *flags |= Node::EndAnchorFlag;
    buf.pop_back();
  }

  Group *group = m_open ? this : m_parent;
  group->m_nodes.push_back(std::make_unique<Token>(buf, *flags));
  *flags = 0;

  if(group->m_type == MatchAny)
    group->m_open = false;

  return group;
}

Filter::Group *Filter::Group::addSubGroup(const Type type, const int flags)
{
  auto newGroup = std::make_unique<Group>(type, flags, this);
  Group *ptr = newGroup.get();
  m_nodes.push_back(std::move(newGroup));

  return ptr;
}

bool Filter::Group::match(const std::vector<std::string> &rows) const
{
  for(const auto &node : m_nodes) {
    if(node->match(rows)) {
      if(m_type == MatchAny)
        return true;
    }
    else if(m_type == MatchAll)
      return test(NotFlag);
  }

  return m_type == MatchAll && !test(NotFlag);
}

Filter::Token::Token(const std::string &buf, int flags)
  : Node(flags), m_buf(buf)
{
  boost::algorithm::to_lower(m_buf);
}

bool Filter::Token::match(const std::vector<std::string> &rows) const
{
  const bool isNot = test(NotFlag);
  bool match = false;

  for(const std::string &row : rows) {
    if(matchRow(row) ^ isNot)
      match = true;
    else if(isNot)
      return false;
  }

  return match;
}

bool Filter::Token::matchRow(const std::string &str) const
{
  const size_t pos = str.find(m_buf);

  if(pos == std::string::npos)
    return false;

  const bool isStart = pos == 0, isEnd = pos + m_buf.size() == str.size();

  if(test(StartAnchorFlag) && !isStart)
    return false;
  if(test(EndAnchorFlag) && !isEnd)
    return false;
  if(test(QuotedFlag) && !test(PhraseFlag)) {
    return
      (isStart || !isalnum(str[pos - 1])) &&
      (isEnd || !isalnum(str[pos + m_buf.size()]));
  }

  return true;
}
