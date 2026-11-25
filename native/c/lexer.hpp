/**
 * This program and the accompanying materials are made available under the terms of the
 * Eclipse Public License v2.0 which accompanies this distribution, and is available at
 * https://www.eclipse.org/legal/epl-v20.html
 *
 * SPDX-License-Identifier: EPL-2.0
 *
 * Copyright Contributors to the Zowe Project.
 *
 */

#ifndef LEXER_HPP
#define LEXER_HPP

#include <cctype>
#include <memory>
#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

namespace lexer
{
/**
 * represents a location within a piece of input or source.
 * includes the filename, line, and column.
 */
class Location
{
public:
  Location()
      : m_filename(""), m_line(1), m_col(1)
  {
  }
  Location(const std::string &filename, size_t line, size_t col)
      : m_filename(filename), m_line(line), m_col(col)
  {
  }

  const std::string &get_filename() const
  {
    return m_filename;
  }
  size_t get_line() const
  {
    return m_line;
  }
  size_t get_col() const
  {
    return m_col;
  }

  void print(std::ostream &os) const
  {
    os << (m_filename.empty() ? "<string>" : m_filename) << " (" << m_line
       << ":" << m_col << ")";
  }

  std::string m_filename;
  size_t m_line;
  size_t m_col;
};

inline std::ostream &operator<<(std::ostream &os, const Location &loc)
{
  loc.print(os);
  return os;
}

/**
 * iterator over an input's characters.
 * keeps track of line and column and can produce a location.
 */
class InputIter
{
public:
  InputIter(const std::string &filename, const std::vector<char> &code)
      : m_input(code), m_filename(filename), m_line(1), m_col(1), m_pos(0)
  {
  }

  char current() const
  {
    if (m_pos < m_input.size())
    {
      return m_input[m_pos];
    }
    return '\0';
  }

  char peek() const
  {
    if (m_pos + 1 < m_input.size())
    {
      return m_input[m_pos + 1];
    }
    return '\0';
  }

  char peek2() const
  {
    if (m_pos + 2 < m_input.size())
    {
      return m_input[m_pos + 2];
    }
    return '\0';
  }

  void next()
  {
    if (m_pos < m_input.size())
    {
      if (m_input[m_pos] == '\n')
      {
        m_line++;
        m_col = 1;
      }
      else if (m_input[m_pos] == '\t')
      {
        m_col += 4;
      }
      else
      {
        m_col++;
      }
      m_pos++;
    }
  }

  void next2()
  {
    next();
    next();
  }

  bool has_more() const
  {
    return m_pos < m_input.size();
  }
  size_t position() const
  {
    return m_pos;
  }
  Location get_location() const
  {
    return Location(m_filename, m_line, m_col);
  }
  size_t get_line() const
  {
    return m_line;
  }
  size_t get_col() const
  {
    return m_col;
  }
  const std::string &get_filename() const
  {
    return m_filename;
  }

private:
  const std::vector<char> &m_input;
  std::string m_filename;
  size_t m_line;
  size_t m_col;
  size_t m_pos;
};

/**
 * represents a piece of input or source code.
 * includes the filename (if applicable) and the raw data.
 */
class Src
{
public:
  Src()
  {
  }

  static Src from_file(const std::string &filename)
  {
    std::ifstream file(filename.c_str(), std::ios::binary);
    if (!file.is_open())
    {
      throw std::runtime_error("could not open file: " + filename);
    }

    file.seekg(0, std::ios::end);
    std::streampos size = file.tellg();
    if (size < 0)
    {
      throw std::runtime_error("error determining size of file: " + filename);
    }
    std::vector<char> buffer(static_cast<size_t>(size));
    file.seekg(0, std::ios::beg);

    if (size > 0)
    {
      file.read(&buffer[0], size);
      if (!file && !file.eof())
      {
        throw std::runtime_error("error reading file: " + filename);
      }
    }

    return Src(filename, buffer);
  }

  static Src from_string(const std::string &code_str,
                         const std::string &filename = "<string>")
  {
    std::vector<char> chars(code_str.begin(), code_str.end());
    return Src(filename, chars);
  }

  const std::string &get_filename() const
  {
    return m_filename;
  }
  const std::vector<char> &get_code() const
  {
    return m_input;
  }

  InputIter get_iterator() const
  {
    return InputIter(m_filename, m_input);
  }

  const char *get_code_ptr() const
  {
    return m_input.empty() ? nullptr : &m_input[0];
  }

private:
  Src(const std::string &filename, const std::vector<char> &code)
      : m_filename(filename), m_input(code)
  {
  }

  std::string m_filename;
  std::vector<char> m_input;
};

enum LexErrorKind
{
  InvalidChar,
  UnclosedString,
  UnknownEscape,
  IntOutOfRange,
  IncompleteInt,
  FloatOutOfRange,
  InvalidFloat
};

// Macro to reduce boilerplate for error factory methods
#define DECLARE_LEX_ERROR(name, kind)         \
  static LexError name(const Location &loc)   \
  {                                           \
    return LexError(loc, LexErrorKind::kind); \
  }

class LexError : public std::exception
{
public:
  LexError(const Location &loc, LexErrorKind kind)
      : m_loc(loc), m_kind(kind)
  {
  }

  DECLARE_LEX_ERROR(invalid_char, InvalidChar)
  DECLARE_LEX_ERROR(unclosed_string, UnclosedString)
  DECLARE_LEX_ERROR(unknown_escape, UnknownEscape)
  DECLARE_LEX_ERROR(int_out_of_range, IntOutOfRange)
  DECLARE_LEX_ERROR(incomplete_int, IncompleteInt)
  DECLARE_LEX_ERROR(float_out_of_range, FloatOutOfRange)
  DECLARE_LEX_ERROR(invalid_float, InvalidFloat)

  const Location &get_location() const
  {
    return m_loc;
  }
  LexErrorKind get_kind() const
  {
    return m_kind;
  }

  virtual const char *what() const throw()
  {
    if (m_msg.empty())
    {
      m_msg = to_string();
    }
    return m_msg.c_str();
  }

  std::string to_string() const
  {
    std::ostringstream ss;
    ss << m_loc << ": ";

    switch (m_kind)
    {
    case LexErrorKind::InvalidChar:
      ss << "invalid character";
      break;
    case LexErrorKind::UnclosedString:
      ss << "unclosed string literal";
      break;
    case LexErrorKind::UnknownEscape:
      ss << "unknown escape character";
      break;
    case LexErrorKind::IntOutOfRange:
      ss << "integer literal out of 64-bit range";
      break;
    case LexErrorKind::IncompleteInt:
      ss << "incomplete integer literal";
      break;
    case LexErrorKind::FloatOutOfRange:
      ss << "floating-point literal out of range";
      break;
    case LexErrorKind::InvalidFloat:
      ss << "invalid floating-point literal";
      break;
    default:
      ss << "unknown lexer error";
      break;
    }

    return ss.str();
  }

private:
  Location m_loc;
  LexErrorKind m_kind;
  mutable std::string m_msg;
};

enum Radix
{
  Dec = 10,
  Bin = 2,
  Hex = 16
};

enum TokenKind
{
  TokEof,

  TokTrue,
  TokFalse,

  TokMinus,
  TokDoubleMinus,
  TokTimes,

  TokId,
  TokIntLit,
  TokFloatLit,
  TokStrLit,
  TokFlagShort, // e.g., -f
  TokFlagLong   // e.g., --force
};

struct Span
{
  size_t start;
  size_t end;

  Span()
      : start(0), end(0)
  {
  }
  Span(size_t s, size_t e)
      : start(s), end(e)
  {
  }
};

struct StringRef
{
  const char *start;
  size_t length;

  std::string to_string() const
  {
    if (!start)
      return "";
    return std::string(start, length);
  }

  void print(std::ostream &os) const
  {
    if (start)
    {
      os.write(start, length);
    }
  }
};

union TokenData
{
  struct IntLit
  {
    long long value;
    Radix base;
  } int_lit;

  struct FloatLit
  {
    double value;
    bool has_exponent;
  } float_lit;

  StringRef string_ref;

  TokenData()
      : string_ref()
  {
  }
  ~TokenData()
  {
  }
};

// no-alloc class to represent lexical tokens
class Token
{
public:
  Token()
      : m_kind(TokEof), m_span()
  {
  }

  // constructor for simple tokens
  Token(TokenKind kind, const Span &span)
      : m_kind(kind), m_span(span)
  {
    m_data.int_lit.value = 0;
    m_data.int_lit.base = Dec;
  }

  Token(const Token &other)
      : m_kind(other.m_kind), m_span(other.m_span)
  {
    copy_data(other);
  }

  Token &operator=(const Token &other)
  {
    if (this != &other)
    {
      m_kind = other.m_kind;
      m_span = other.m_span;
      copy_data(other);
    }
    return *this;
  }

  // todo: may be unneeded
  ~Token()
  {
  }

  // static factory methods for complex tokens below

  static Token make_id(const char *start_ptr, size_t len, const Span &span)
  {
    Token token(TokId, span);
    token.m_data.string_ref.start = start_ptr;
    token.m_data.string_ref.length = len;
    return token;
  }

  static Token make_short_flag(const char *name_ptr, size_t len,
                               const Span &span)
  {
    Token token(TokFlagShort, span);
    token.m_data.string_ref.start = name_ptr;
    token.m_data.string_ref.length = len;
    return token;
  }

  static Token make_long_flag(const char *name_ptr, size_t len,
                              const Span &span)
  {
    Token token(TokFlagLong, span);
    token.m_data.string_ref.start = name_ptr;
    token.m_data.string_ref.length = len;
    return token;
  }

  static Token make_int_lit(long long value, Radix base, const Span &span)
  {
    Token token(TokIntLit, span);
    token.m_data.int_lit.value = value;
    token.m_data.int_lit.base = base;
    return token;
  }

  static Token make_float_lit(double value, bool has_exponent,
                              const Span &span)
  {
    Token token(TokFloatLit, span);
    token.m_data.float_lit.value = value;
    token.m_data.float_lit.has_exponent = has_exponent;
    return token;
  }

  // stores raw string content (pointer between quotes, length).
  // escape sequences are processed on demand by get_str_lit_value().
  static Token make_str_lit(const char *content_start_ptr, size_t content_len,
                            const Span &span)
  {
    Token token(TokStrLit, span);
    token.m_data.string_ref.start = content_start_ptr;
    token.m_data.string_ref.length = content_len;
    return token;
  }

  TokenKind get_kind() const
  {
    return m_kind;
  }
  const Span &get_span() const
  {
    return m_span;
  }

  // getter methods for complex token data below

  // identifier/flag content (raw pointer and length)
  const char *get_string_ref_start() const
  {
    // check for kinds that use StringRef
    if (m_kind == TokId || m_kind == TokStrLit || m_kind == TokFlagShort ||
        m_kind == TokFlagLong)
    {
      return m_data.string_ref.start;
    }
    return nullptr;
  }

  size_t get_string_ref_length() const
  {
    // check for kinds that use StringRef
    if (m_kind == TokId || m_kind == TokStrLit || m_kind == TokFlagShort ||
        m_kind == TokFlagLong)
    {
      return m_data.string_ref.length;
    }
    return 0u;
  }

  // helper to get identifier/flag value as std::string (allocates)
  std::string get_id_value() const
  {
    // note: this returns the name of the flag, not including dashes.
    if (m_kind == TokId || m_kind == TokFlagShort || m_kind == TokFlagLong)
    {
      if (m_data.string_ref.start)
      {
        return std::string(m_data.string_ref.start, m_data.string_ref.length);
      }
      else
      {
        return "";
      }
    }
    // todo: consider throwing or returning a sentinel
    throw std::runtime_error("token is not an identifier or flag");
  }

  long long get_int_value() const
  {
    if (m_kind == TokIntLit)
    {
      return m_data.int_lit.value;
    }
    // todo: consider throwing or returning a sentinel
    throw std::runtime_error("token is not an integer literal");
  }

  Radix get_int_base() const
  {
    if (m_kind == TokIntLit)
    {
      return m_data.int_lit.base;
    }
    // todo: consider throwing or returning a sentinel
    throw std::runtime_error("token is not an integer literal");
  }

  double get_float_value() const
  {
    if (m_kind == TokFloatLit)
    {
      return m_data.float_lit.value;
    }
    // todo: consider throwing or returning a sentinel
    throw std::runtime_error("token is not a float literal");
  }

  bool has_float_exponent() const
  {
    if (m_kind == TokFloatLit)
    {
      return m_data.float_lit.has_exponent;
    }
    // todo: consider throwing or returning a sentinel
    throw std::runtime_error("token is not a float literal");
  }

  // get a string literal value (processes escapes, allocates a new std::string)
  std::string get_str_lit_value() const
  {
    if (m_kind != TokStrLit)
    {
      throw std::runtime_error("token is not a string literal");
    }
    if (!m_data.string_ref.start)
      return ""; // should not happen if created correctly

    std::string processed_value;
    // estimate capacity, might overestimate if many escapes
    processed_value.reserve(m_data.string_ref.length);
    const char *ptr = m_data.string_ref.start;
    const char *end = ptr + m_data.string_ref.length;

    while (ptr < end)
    {
      if (*ptr == '\\')
      {
        ptr++; // consume backslash
        if (ptr >= end)
        {
          // this indicates an error caught by lexer or malformed raw slice
          // for robustness, stop processing here and add a literal backslash
          processed_value += '\\';
          break;
        }
        switch (*ptr)
        {
        case 'n':
          processed_value += '\n';
          break;
        case 'r':
          processed_value += '\r';
          break;
        case 't':
          processed_value += '\t';
          break;
        case '\\':
          processed_value += '\\';
          break;
        case '"':
          processed_value += '"';
          break;
        case '0':
          processed_value += '\0';
          break;
        // add other escapes if supported by the language
        default:
          // unknown escape was already checked by lexer.
          // if it got here, maybe append the literal char after backslash?
          processed_value += *ptr;
          break;
        }
      }
      else
      {
        // should not encounter unescaped double quotes here as they delimit the
        // raw slice. if found, it's an internal error. add it literally for
        // now.
        processed_value += *ptr;
      }
      ptr++;
    }
    return processed_value;
  }

  // print token to stream
  void print(std::ostream &os) const
  {
    if (print_flag_token(os))
      return;

    switch (m_kind)
    {
    case TokId:
      os << get_id_value();
      break;
    case TokStrLit:
      print_string_literal(os);
      break;
    case TokIntLit:
      print_integer_literal(os);
      break;
    case TokFloatLit:
      print_float_literal(os);
      break;
    case TokFalse:
      os << "false";
      break;
    case TokTrue:
      os << "true";
      break;
    default:
      os << "<unknown token_kind: " << static_cast<int>(m_kind) << ">";
      break;
    }
  }

private:
  // Helper methods for printing tokens
  static const char *get_simple_token_str(TokenKind kind)
  {
    switch (kind)
    {
    case TokEof:
      return "<eof>";
    case TokTrue:
      return "true";
    case TokFalse:
      return "false";
    case TokMinus:
      return "-";
    case TokDoubleMinus:
      return "--";
    case TokTimes:
      return "*";
    case TokId:
      return "<id>";
    case TokIntLit:
      return "<int_lit>";
    case TokFloatLit:
      return "<float_lit>";
    case TokStrLit:
      return "<str_lit>";
    case TokFlagShort:
      return "<short_flag>";
    case TokFlagLong:
      return "<long_flag>";
    default:
      return nullptr;
    }
  }

  bool print_flag_token(std::ostream &os) const
  {
    if (m_kind == TokFlagShort)
    {
      os << "-";
      print_string_ref(os, m_data.string_ref, "<null_short_flag>");
      return true;
    }
    if (m_kind == TokFlagLong)
    {
      os << "--";
      print_string_ref(os, m_data.string_ref, "<null_long_flag>");
      return true;
    }
    return false;
  }

  bool print_simple_token(std::ostream &os) const
  {
    const char *str = get_simple_token_str(m_kind);
    if (str)
    {
      os << str;
      return true;
    }
    return false;
  }

  static void print_string_ref(std::ostream &os, const StringRef &ref, const char *null_fallback)
  {
    if (ref.start)
      os.write(ref.start, ref.length);
    else
      os << null_fallback;
  }

  void print_string_literal(std::ostream &os) const
  {
    os << "\"";
    if (m_data.string_ref.start)
    {
      const char *ptr = m_data.string_ref.start;
      const char *end = ptr + m_data.string_ref.length;
      while (ptr < end)
      {
        char c = *ptr++;
        if (c == '\\' && ptr < end)
        {
          switch (*ptr++)
          {
          case 'n':
            os << "\\n";
            break;
          case 'r':
            os << "\\r";
            break;
          case 't':
            os << "\\t";
            break;
          case '\\':
            os << "\\\\";
            break;
          case '"':
            os << "\\\"";
            break;
          case '0':
            os << "\\0";
            break;
          default:
            os << '\\' << *(ptr - 1);
            break;
          }
        }
        else if (c == '"')
          os << "\\\"";
        else if (c == '\n')
          os << "\\n";
        else if (c == '\r')
          os << "\\r";
        else if (c == '\t')
          os << "\\t";
        else if (std::isprint(static_cast<unsigned char>(c)))
          os << c;
        else
          os << "\x3F";
      }
    }
    os << "\"";
  }

  void print_integer_literal(std::ostream &os) const
  {
    if (m_data.int_lit.base == Hex)
    {
      std::ios_base::fmtflags flags = os.flags();
      os << "0x" << std::hex << m_data.int_lit.value;
      os.flags(flags);
    }
    else if (m_data.int_lit.base == Bin)
    {
      os << "0b";
      if (m_data.int_lit.value == 0)
        os << "0";
      else
      {
        bool leading = true;
        for (int i = 63; i >= 0; --i)
        {
          if ((m_data.int_lit.value >> i) & 1)
          {
            leading = false;
            os << '1';
          }
          else if (!leading)
            os << '0';
        }
      }
    }
    else
      os << m_data.int_lit.value;
  }

  void print_float_literal(std::ostream &os) const
  {
    std::ios_base::fmtflags flags = os.flags();
    std::streamsize precision = os.precision();
    if (m_data.float_lit.has_exponent)
      os.setf(std::ios::scientific, std::ios::floatfield);
    os << m_data.float_lit.value;
    os.flags(flags);
    os.precision(precision);
  }

  // helper to copy data based on kind (used by copy ctor and assignment)
  // this should be safe now, as it relies on the (trivial) copy/assignment
  // of the member structs (IntLit, FloatLit, StringRef).
  void copy_data(const Token &other)
  {
    switch (other.m_kind)
    {
    case TokId:
    case TokStrLit:
    case TokFlagShort:
    case TokFlagLong:
      // assign StringRef (member-wise copy)
      m_data.string_ref = other.m_data.string_ref;
      break;
    case TokIntLit:
      // assign IntLit struct (member-wise copy)
      m_data.int_lit = other.m_data.int_lit;
      break;
    case TokFloatLit:
      // assign FloatLit struct (member-wise copy)
      m_data.float_lit = other.m_data.float_lit;
      break;
    default:
      // for simple tokens or TokEof, the data doesn't strictly matter,
      // but copying something ensures defined state
      m_data.int_lit = other.m_data.int_lit;
      break;
    }
  }

  TokenKind m_kind;
  Span m_span;
  TokenData m_data; // union holding complex data (value types or StringRef)
};

// stream operator for token
inline std::ostream &operator<<(std::ostream &os, const Token &token)
{
  token.print(os);
  return os;
}

// facility for tokenizing source code or input strings
class Lexer
{
public:
  /**
   * primary static function to tokenize source (input, code, etc.).
   * takes a source object and returns a vector of tokens.
   * note: source object must remain valid while tokens are used.
   */
  static std::vector<Token> tokenize(const Src &source)
  {
    Lexer lexer(source);
    lexer.lex_all();
    return lexer.m_tokens;
  }
  static size_t quoted_value_end(const std::string &value)
  {
    Src src = Src::from_string(value, "<quoted>");
    Lexer lexer(src);
    return lexer.lex_string(0).get_span().end;
  }

private:
  // private constructor - only used internally by the static tokenize method
  Lexer(const Src &source)
      : m_iter(source.get_iterator()), m_input_ptr(source.get_code_ptr())
  {
    size_t estimated_tokens = source.get_code().size() / 4;
    if (estimated_tokens > 8)
    {
      m_tokens.reserve(estimated_tokens);
    }
  }

  static bool is_ascii_dec_digit(char c)
  {
    return c >= '0' && c <= '9';
  }

  static bool is_ascii_hex_digit(char c)
  {
    return is_ascii_dec_digit(c) || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
  }

  static bool is_ascii_bin_digit(char c)
  {
    return c == '0' || c == '1';
  }

  static bool is_ident_start(char c)
  {
    unsigned char uc = static_cast<unsigned char>(c);
    return uc != ' ' && iscntrl(uc) == 0;
  }

  static bool is_ident_cont(char c)
  {
    return is_ident_start(c);
  }

  // combined digit check for lex_number
  static bool is_digit_or_underscore(char c, Radix base)
  {
    if (c == '_')
      return true;
    switch (base)
    {
    case Dec:
      return is_ascii_dec_digit(c);
    case Hex:
      return is_ascii_hex_digit(c);
    case Bin:
      return is_ascii_bin_digit(c);
    default:
      return false;
    }
  }

  // core lexing driver function
  void lex_all()
  {
    m_tokens.clear(); // clear if lexer object was reused
    while (true)
    {
      eat_whitespace_and_comments();
      Token token = next_token();
      m_tokens.push_back(token);
      if (token.get_kind() == TokEof)
      {
        break;
      }
    }
  }

  // skips over whitespace and single-line comments
  void eat_whitespace_and_comments()
  {
    while (true)
    {
      char c = current();

      // Fast path for common whitespace
      if (c == ' ' || c == '\t' || c == '\n' || c == '\r')
      {
        next();
        continue;
      }

      // Check for comments at end of line in format `# <comment>`
      if (c == '#' && peek() == ' ')
      {
        // consume # to end of line
        next();
        while (true)
        {
          char comm_c = current();
          if (comm_c == '\n' || comm_c == '\0')
            break;
          next();
        }
        continue;
      }

      // No more whitespace or comments
      return;
    }
  }

  // lexes the next token from the input stream
  Token next_token()
  {
    size_t start_pos = m_iter.position();
    Location start_loc = m_iter.get_location(); // get location at start

    char c = current();
    switch (c)
    {
    // eof
    case '\0':
      return Token(TokEof, Span(start_pos, start_pos));

    // single-character tokens that are unambiguous
    case '*':
      next();
      return Token(TokTimes, Span(start_pos, m_iter.position()));

    // potentially multi-character tokens
    case '-':
      next(); // consume '-'
      if (current() == '-')
      {
        next(); // consume second '-'
        if (is_ident_start(current()))
        {
          return lex_long_flag(start_pos);
        }
        else
        {
          return Token(TokDoubleMinus, Span(start_pos, m_iter.position()));
        }
      }
      else if (is_ident_start(current()) || is_ascii_dec_digit(current()))
      {
        return lex_short_flag(start_pos);
      }
      else
      {
        return Token(TokMinus, Span(start_pos, m_iter.position()));
      }

    // string literals
    case '"':
      return lex_string(start_pos); // pass start position

    // numbers (integers or floats)
    case '0':
    case '1':
    case '2':
    case '3':
    case '4':
    case '5':
    case '6':
    case '7':
    case '8':
    case '9':
      return lex_number(start_pos); // pass start position

    // identifiers or keywords
    default:
      if (is_ident_start(c))
      {
        return lex_identifier_or_keyword(start_pos); // pass start position
      }
      // unknown character
      throw LexError::invalid_char(start_loc); // use location at start of char
    }
  }

  // lexes an identifier or a keyword
  Token lex_identifier_or_keyword(size_t start_pos)
  {
    // start_pos is the position of the first character
    // current() is the first character
    next(); // consume the start character

    while (is_ident_cont(current()))
    {
      next();
    }
    size_t end_pos = m_iter.position();
    size_t length = end_pos - start_pos;
    const char *id_start_ptr = m_input_ptr + start_pos;

    if (length == 4 && id_start_ptr[0] == 't' && id_start_ptr[1] == 'r' &&
        id_start_ptr[2] == 'u' && id_start_ptr[3] == 'e')
    {
      return Token(TokTrue, Span(start_pos, end_pos));
    }
    if (length == 5 && id_start_ptr[0] == 'f' && id_start_ptr[1] == 'a' &&
        id_start_ptr[2] == 'l' && id_start_ptr[3] == 's' &&
        id_start_ptr[4] == 'e')
    {
      return Token(TokFalse, Span(start_pos, end_pos));
    }

    // not a keyword, it's an identifier
    return Token::make_id(id_start_ptr, length, Span(start_pos, end_pos));
  }

  // lexes a string literal enclosed in double quotes
  // returns token with raw content slice (pointer/length between quotes)
  Token lex_string(size_t span_start)
  {
    // span_start is position of opening quote "
    Location start_loc = m_iter.get_location();   // location of "
    next();                                       // consume the opening quote "
    size_t content_start_pos = m_iter.position(); // position after "

    while (true)
    {
      Location char_loc = m_iter.get_location(); // location of current char
      char c = current();
      if (c == '\0')
      {
        // error location: ideally point to the opening quote or where eof
        // encountered
        throw LexError::unclosed_string(start_loc);
      }
      if (c == '\n')
      {
        // strings cannot contain raw newlines (adjust if language allows)
        throw LexError::unclosed_string(char_loc); // error at the newline
      }
      if (c == '"')
      {
        break; // end of string content
      }

      if (c == '\\')
      {                                              // escape sequence
        Location escape_loc = m_iter.get_location(); // location of backslash
        next();                                      // consume backslash
        char escaped_char = current();
        if (escaped_char == '\0' ||
            escaped_char == '\n')
        {                                              // invalid state after backslash
          throw LexError::unclosed_string(escape_loc); // error at the backslash
        }
        // validate escape sequence based on language rules
        switch (escaped_char)
        {
        case 'n':
        case 'r':
        case 't':
        case '\\':
        case '"':
        case '0':
          // valid escapes in this language
          break;
        // add other valid escapes (e.g., \x_hh, \u_hhhh) if needed
        default:
          // unknown escape sequence
          throw LexError::unknown_escape(
              m_iter.get_location()); // error at char after backslash
        }
        next(); // consume the character after backslash
      }
      else
      {
        next(); // consume regular character
      }
    }

    size_t content_end_pos = m_iter.position(); // position of closing quote "
    next();                                     // consume closing quote "
    size_t span_end = m_iter.position();

    const char *content_start_ptr = m_input_ptr + content_start_pos;
    size_t content_length = content_end_pos - content_start_pos;

    return Token::make_str_lit(content_start_ptr, content_length,
                               Span(span_start, span_end));
  }

  // lexes a number (integer or float)
  Token lex_number(size_t start_pos)
  {
    Location start_loc = m_iter.get_location();
    Radix base = Dec;
    bool is_float = false;
    bool has_exponent = false;

    char num_buffer[128];
    int buffer_idx = 0;
    const int buffer_max_idx = sizeof(num_buffer) - 1;

    // Check for base prefixes (0x, 0b)
    if (current() == '0')
    {
      if (buffer_idx < buffer_max_idx)
      {
        num_buffer[buffer_idx++] = '0';
      }
      next();
      char p = current();
      if (p == 'x' || p == 'X')
      {
        base = Hex;
        if (buffer_idx < buffer_max_idx)
        {
          num_buffer[buffer_idx++] = p;
        }
        next();
        if (!is_ascii_hex_digit(current()))
        {
          throw LexError::incomplete_int(m_iter.get_location());
        }
      }
      else if (p == 'b' || p == 'B')
      {
        base = Bin;
        if (buffer_idx < buffer_max_idx)
        {
          num_buffer[buffer_idx++] = p;
        }
        next();
        if (!is_ascii_bin_digit(current()))
        {
          throw LexError::incomplete_int(m_iter.get_location());
        }
      }
      else if (!is_ascii_dec_digit(p) && p != '.' && p != 'e' && p != 'E')
      {
        // Just '0' followed by non-numeric/non-float chars
        num_buffer[buffer_idx] = '\0';
        return Token::make_int_lit(0, Dec, Span(start_pos, m_iter.position()));
      }
    }

    // Parse main digits
    while (is_digit_or_underscore(current(), base))
    {
      if (current() != '_')
      {
        if (buffer_idx < buffer_max_idx)
        {
          num_buffer[buffer_idx++] = current();
        }
      }
      next();
    }

    // Check for float components (only if base is decimal)
    if (base == Dec)
    {
      // Decimal point
      if (current() == '.' && is_ascii_dec_digit(peek()))
      {
        is_float = true;
        if (buffer_idx < buffer_max_idx)
        {
          num_buffer[buffer_idx++] = '.';
        }
        next();
        while (is_digit_or_underscore(current(), Dec))
        {
          if (current() != '_')
          {
            if (buffer_idx < buffer_max_idx)
            {
              num_buffer[buffer_idx++] = current();
            }
          }
          next();
        }
      }

      // Exponent
      if (current() == 'e' || current() == 'E')
      {
        char exp_peek = peek();
        char exp_peek2 = peek2();

        if (is_ascii_dec_digit(exp_peek) ||
            ((exp_peek == '+' || exp_peek == '-') && is_ascii_dec_digit(exp_peek2)))
        {
          is_float = true;
          has_exponent = true;
          if (buffer_idx < buffer_max_idx)
          {
            num_buffer[buffer_idx++] = current();
          }
          next();

          if (current() == '+' || current() == '-')
          {
            if (buffer_idx < buffer_max_idx)
            {
              num_buffer[buffer_idx++] = current();
            }
            next();
          }

          while (is_digit_or_underscore(current(), Dec))
          {
            if (current() != '_')
            {
              if (buffer_idx < buffer_max_idx)
              {
                num_buffer[buffer_idx++] = current();
              }
            }
            next();
          }
        }
      }
    }
    else if (current() == '.' || current() == 'e' || current() == 'E')
    {
      throw LexError::invalid_char(m_iter.get_location());
    }

    num_buffer[buffer_idx] = '\0';

    // Validate we have actual digits after prefix
    const char *digits_start = num_buffer;
    if (base == Hex || base == Bin)
    {
      if (buffer_idx <= 2)
      {
        throw LexError::incomplete_int(m_iter.get_location());
      }
      digits_start += 2; // Skip prefix
    }

    if (*digits_start == '\0')
    {
      throw LexError::incomplete_int(start_loc);
    }

    size_t end_pos = m_iter.position();
    Span number_span(start_pos, end_pos);

    if (is_float)
    {
      errno = 0;
      char *endptr;
      double value = strtod(num_buffer, &endptr);

      if (errno == ERANGE)
      {
        throw LexError::float_out_of_range(m_iter.get_location());
      }
      if (endptr != num_buffer + buffer_idx)
      {
        throw LexError::invalid_float(start_loc);
      }

      return Token::make_float_lit(value, has_exponent, number_span);
    }
    else
    {
      errno = 0;
      char *endptr;
      int strtoll_base = (base == Hex) ? 16 : (base == Bin) ? 2
                                                            : 10;
      long long value = strtoll(digits_start, &endptr, strtoll_base);

      if (errno == ERANGE)
      {
        throw LexError::int_out_of_range(m_iter.get_location());
      }

      const char *expected_end = num_buffer + buffer_idx;
      if (endptr != expected_end)
      {
        throw LexError::incomplete_int(start_loc);
      }

      return Token::make_int_lit(value, base, number_span);
    }
  }

  // lexes a short flag (e.g., -v, -f)
  // assumes called when current() is the char *after* '-'
  Token lex_short_flag(size_t start_pos)
  {
    size_t content_start_pos = m_iter.position();

    while (is_ident_cont(current()) || is_ascii_dec_digit(current()))
    {
      next();
    }

    size_t end_pos = m_iter.position();
    size_t length = end_pos - content_start_pos;
    const char *flag_start_ptr = m_input_ptr + content_start_pos;

    return Token::make_short_flag(flag_start_ptr, length, Span(start_pos, end_pos));
  }

  // lexes a long flag (e.g., --version, --file)
  // assumes called when current() is the char *after* '--'
  Token lex_long_flag(size_t start_pos)
  {
    size_t name_start_pos = m_iter.position();

    while (is_ident_cont(current()) || current() == '-')
    {
      next();
    }

    size_t name_end_pos = m_iter.position();
    size_t name_length = name_end_pos - name_start_pos;
    const char *name_start_ptr = m_input_ptr + name_start_pos;

    return Token::make_long_flag(name_start_ptr, name_length, Span(start_pos, name_end_pos));
  }

  // character navigation helpers (inline wrappers around iterator)
  // could be made inline if compiler doesn't already do it.
  char current() const
  {
    return m_iter.current();
  }
  char peek() const
  {
    return m_iter.peek();
  }
  char peek2() const
  {
    return m_iter.peek2();
  } // use iterator's peek2

  void next()
  {
    m_iter.next();
  }
  void next2()
  {
    m_iter.next2();
  }

  // state
  InputIter m_iter;            // iterator over the input
  const char *m_input_ptr;     // pointer to start of the input
  std::vector<Token> m_tokens; // vector to store the generated tokens
};

} // namespace lexer

#endif // LEXER_HPP
