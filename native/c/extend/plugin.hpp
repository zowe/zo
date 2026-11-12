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

#ifndef PLUGIN_HPP
#define PLUGIN_HPP

#include <algorithm>
#include <memory>
#include <iostream>
#include <ostream>
#include <sstream>
#include <string>
#include <vector>
#include <cstddef>
#include <set>

// Ordered containers, not hashed: std::hash<std::string> resolves to the out-of-line libc++ helper
// std::__1_e::__hash_memory, absent from CRTEQCXE on z/OS systems below the required Language
// Environment maintenance level (CEE3561S at load time). See zowex#871 and doc/troubleshooting.md.
#include <map>

// Binary compatibility contract between zowex and out-of-tree plug-ins.
//
// Bump this on any change to the layout of a type that crosses the dlopen boundary - notably
// ast::ObjMap, ast::Ast, plugin::ArgumentMap, plugin::Io, plugin::InvocationContext and
// plugin::PluginManager. Accessors such as InvocationContext::get<T>() are inlined *into* the
// plug-in, so a plug-in compiled against a different layout reads the wrong member offsets;
// zowex rejects such a plug-in at load time rather than letting it corrupt memory.
//
// Version 1 was the implicit, unversioned contract before this constant existed. A plug-in that
// does not export zo_plugin_abi_version() reports as version 0 and is rejected.
constexpr unsigned int ZO_PLUGIN_ABI_VERSION = 3u;

// Plug-ins must expand this once, in the same translation unit as register_plugin().
#define ZO_PLUGIN_DECLARE_ABI()                   \
  extern "C" unsigned int zo_plugin_abi_version() \
  {                                               \
    return ZO_PLUGIN_ABI_VERSION;                 \
  }

template <typename Interface>
class Factory
{
public:
  virtual ~Factory()
  {
  }
  virtual std::unique_ptr<Interface> create() = 0;
};

namespace ast
{
struct Ast;

using Node = std::shared_ptr<Ast>;
using StringPtr = std::shared_ptr<std::string>;
using VecPtr = std::shared_ptr<std::vector<Node>>;
using ObjMap = std::map<std::string, Node>;
using ObjPtr = std::shared_ptr<ObjMap>;

struct Ast
{
  enum Kind
  {
    Null,
    Boolean,
    Integer,
    Number,
    String,
    Array,
    Object
  };

  // --- factories ---
  static Node null()
  {
    return Node(new Ast(Null));
  }
  static Node boolean(bool v)
  {
    Node n(new Ast(Boolean));
    n->b = v;
    return n;
  }
  static Node integer(long long v)
  {
    Node n(new Ast(Integer));
    n->i = v;
    return n;
  }
  static Node number(double v)
  {
    Node n(new Ast(Number));
    n->d = v;
    return n;
  }
  static Node string(const std::string &v)
  {
    Node n(new Ast(String));
    n->s.reset(new std::string(v));
    return n;
  }
  static Node array()
  {
    Node n(new Ast(Array));
    n->a.reset(new std::vector<Node>());
    return n;
  }
  static Node object()
  {
    Node n(new Ast(Object));
    n->o.reset(new ObjMap());
    return n;
  }

  // --- kind checks ---
  Kind kind() const
  {
    return k;
  }
  bool is_null() const
  {
    return k == Null;
  }
  bool is_bool() const
  {
    return k == Boolean;
  }
  bool is_integer() const
  {
    return k == Integer;
  }
  bool is_number() const
  {
    return k == Number || k == Integer;
  }
  bool is_string() const
  {
    return k == String;
  }
  bool is_array() const
  {
    return k == Array;
  }
  bool is_object() const
  {
    return k == Object;
  }

  // --- accessors (const) ---
  bool as_bool() const
  {
    require(Boolean, "bool");
    return b;
  }
  long long as_integer() const
  {
    require(Integer, "integer");
    return i;
  }
  double as_number() const
  {
    return k == Integer ? (double)i : (require(Number, "number"), d);
  }
  const std::string &as_string() const
  {
    require(String, "string");
    return *s;
  }
  const std::vector<Node> &as_array() const
  {
    require(Array, "array");
    return *a;
  }
  const ObjMap &as_object() const
  {
    require(Object, "object");
    return *o;
  }

  // --- accessors (mutable) ---
  std::vector<Node> &array_ref()
  {
    require(Array, "array");
    return *a;
  }
  ObjMap &object_ref()
  {
    require(Object, "object");
    return *o;
  }

  Ast *set(const std::string &key, const Node &value)
  {
    require(Object, "object");
    (*o)[key] = value;
    return this;
  }
  Ast *push(const Node &value)
  {
    require(Array, "array");
    a->push_back(value);
    return this;
  }

  // --- lookups ---
  Node get(const std::string &key) const
  {
    require(Object, "object");
    auto it = o->find(key);
    return it == o->end() ? Node() : it->second;
  }
  const Node &at(size_t idx) const
  {
    require(Array, "array");
    if (idx >= a->size())
      throw std::out_of_range("json array index");
    return (*a)[idx];
  }

  std::string as_json() const
  {
    switch (k)
    {
    case Null:
      return "null";
    case Boolean:
      return b ? "true" : "false";
    case Integer:
    {
      std::stringstream ss;
      ss << i;
      return ss.str();
    }
    case Number:
    {
      std::stringstream ss;
      ss << d;
      return ss.str();
    }
    case String:
      return '"' + *s + '"';
    case Array:
    {
      std::string out = "[";
      for (size_t i = 0; i < a->size(); ++i)
      {
        if (i)
          out += ", ";
        out += (*a)[i] ? (*a)[i]->as_json() : "null";
      }
      out += "]";
      return out;
    }
    case Object:
    {
      std::string out = "{";
      bool first = true;
      for (auto it = o->begin(); it != o->end(); ++it)
      {
        if (!first)
          out += ", ";
        first = false;
        out += '"' + it->first + '"';
        out += ": ";
        out += it->second ? it->second->as_json() : "null";
      }
      out += "}";
      return out;
    }
    }
    return "null";
  }

  std::string as_yaml() const
  {
    std::ostringstream out;
    append_yaml(out, 0);
    return out.str();
  }

  std::string debug() const
  {
    return as_yaml();
  }

private:
  explicit Ast(Kind kind_)
      : k(kind_), b(false), i(0), d(0.0)
  {
  }

  void append_yaml(std::ostringstream &out, size_t indent) const
  {
    switch (k)
    {
    case Null:
    case Boolean:
    case Integer:
    case Number:
    case String:
      out << as_json();
      return;
    case Array:
      append_yaml_array(out, indent);
      return;
    case Object:
      append_yaml_object(out, indent);
      return;
    }
    out << "null";
  }

  void append_yaml_array(std::ostringstream &out, size_t indent) const
  {
    if (!a || a->empty())
    {
      out << "[]";
      return;
    }

    for (size_t idx = 0; idx < a->size(); ++idx)
    {
      if (idx)
        out << '\n';

      append_indent(out, indent);
      const Node &elem = (*a)[idx];
      if (!elem)
      {
        out << "- null";
      }
      else if (elem->needs_multiline_yaml())
      {
        out << "-\n";
        elem->append_yaml(out, indent + 2);
      }
      else
      {
        out << "- ";
        elem->append_yaml(out, 0);
      }
    }
  }

  void append_yaml_object(std::ostringstream &out, size_t indent) const
  {
    if (!o || o->empty())
    {
      out << "{}";
      return;
    }

    bool first = true;
    const auto &obj = *o;
    for (auto entry = obj.begin(); entry != obj.end(); ++entry)
    {
      if (!first)
        out << '\n';
      first = false;

      append_indent(out, indent);
      out << entry->first;
      const Node &value = entry->second;
      if (!value)
      {
        out << ": null";
      }
      else if (value->needs_multiline_yaml())
      {
        out << ":\n";
        value->append_yaml(out, indent + 2);
      }
      else
      {
        out << ": ";
        value->append_yaml(out, 0);
      }
    }
  }

  bool needs_multiline_yaml() const
  {
    if (k == Array)
      return a && !a->empty();
    if (k == Object)
      return o && !o->empty();
    return false;
  }

  static void append_indent(std::ostringstream &out, size_t indent)
  {
    for (size_t idx = 0; idx < indent; ++idx)
      out << ' ';
  }

  void require(Kind expected, const char *name) const
  {
    if (k != expected)
      throw std::logic_error(std::string("Json: expected ") + name);
  }

  Kind k;
  bool b;      // Boolean
  long long i; // Integer
  double d;    // Number
  StringPtr s; // String
  VecPtr a;    // Array
  ObjPtr o;    // Object
};

// convenience free functions
inline Node obj()
{
  return Ast::object();
}
inline Node arr()
{
  return Ast::array();
}
inline Node str(const std::string &v)
{
  return Ast::string(v);
}
inline Node num(double v)
{
  return Ast::number(v);
}
inline Node i64(long long v)
{
  return Ast::integer(v);
}
inline Node boolean(bool v)
{
  return Ast::boolean(v);
}
inline Node nil()
{
  return Ast::null();
}

} // namespace ast

namespace parser
{
class Command;
typedef std::shared_ptr<Command> command_ptr;
} // namespace parser

namespace plugin
{

class Argument
{
public:
  enum ValueKind
  {
    ValueKind_None,
    ValueKind_Bool,
    ValueKind_Int,
    ValueKind_Double,
    ValueKind_String,
    ValueKind_List
  };

  Argument()
      : m_kind(ValueKind_None), m_is_dynamic(false)
  {
    m_value.i = 0;
  }

  explicit Argument(bool val)
      : m_kind(ValueKind_Bool), m_is_dynamic(false)
  {
    m_value.b = val;
  }

  explicit Argument(long long val)
      : m_kind(ValueKind_Int), m_is_dynamic(false)
  {
    m_value.i = val;
  }

  explicit Argument(double val)
      : m_kind(ValueKind_Double), m_is_dynamic(false)
  {
    m_value.d = val;
  }

  explicit Argument(const std::string &val)
      : m_kind(ValueKind_String), m_is_dynamic(false)
  {
    m_value.s = new std::string(val);
  }

  explicit Argument(const char *val)
      : m_kind(ValueKind_String), m_is_dynamic(false)
  {
    m_value.s = val ? new std::string(val) : new std::string();
  }

  explicit Argument(const std::vector<std::string> &val)
      : m_kind(ValueKind_List), m_is_dynamic(false)
  {
    m_value.sv = new std::vector<std::string>(val);
  }

  Argument(const Argument &other)
      : m_kind(ValueKind_None), m_is_dynamic(false)
  {
    copy_from(other);
  }

  Argument &operator=(const Argument &other)
  {
    if (this != &other)
    {
      clear();
      copy_from(other);
    }
    return *this;
  }

  Argument(Argument &&other) noexcept
      : m_kind(ValueKind_None), m_is_dynamic(false)
  {
    move_from(other);
  }

  Argument &operator=(Argument &&other) noexcept
  {
    if (this != &other)
    {
      clear();
      move_from(other);
    }
    return *this;
  }

  ~Argument()
  {
    clear();
  }

  ValueKind get_kind() const
  {
    return m_kind;
  }

  bool is_none() const
  {
    return m_kind == ValueKind_None;
  }

  bool is_bool() const
  {
    return m_kind == ValueKind_Bool;
  }

  bool is_int() const
  {
    return m_kind == ValueKind_Int;
  }

  bool is_double() const
  {
    return m_kind == ValueKind_Double;
  }

  bool is_string() const
  {
    return m_kind == ValueKind_String;
  }

  bool is_string_vector() const
  {
    return m_kind == ValueKind_List;
  }

  const bool *get_bool() const
  {
    return m_kind == ValueKind_Bool ? &m_value.b : nullptr;
  }

  const long long *get_int() const
  {
    return m_kind == ValueKind_Int ? &m_value.i : nullptr;
  }

  const double *get_double() const
  {
    return m_kind == ValueKind_Double ? &m_value.d : nullptr;
  }

  const std::string *get_string() const
  {
    return m_kind == ValueKind_String ? m_value.s : nullptr;
  }

  const std::vector<std::string> *get_string_vector() const
  {
    return m_kind == ValueKind_List ? m_value.sv : nullptr;
  }

  std::string *get_string_mutable()
  {
    return m_kind == ValueKind_String ? m_value.s : nullptr;
  }

  std::vector<std::string> *get_string_vector_mutable()
  {
    return m_kind == ValueKind_List ? m_value.sv : nullptr;
  }

  std::string get_string_value(const std::string &default_val = "") const
  {
    const std::string *ptr = get_string();
    return ptr ? *ptr : default_val;
  }

  void print(std::ostream &os) const
  {
    switch (m_kind)
    {
    case ValueKind_None:
      os << "<none>";
      break;
    case ValueKind_Bool:
      os << (m_value.b ? "true" : "false");
      break;
    case ValueKind_Int:
      os << m_value.i;
      break;
    case ValueKind_Double:
      os << m_value.d;
      break;
    case ValueKind_String:
      os << (m_value.s ? *m_value.s : "<invalid_string>");
      break;
    case ValueKind_List:
    {
      os << "[";
      if (m_value.sv)
      {
        for (size_t j = 0; j < m_value.sv->size(); ++j)
        {
          os << (*m_value.sv)[j];
          if (j + 1 < m_value.sv->size())
            os << ", ";
        }
      }
      os << "]";
      break;
    }
    }
  }

  bool is_dynamic() const
  {
    return m_is_dynamic;
  }
  void set_dynamic(bool is_dynamic)
  {
    m_is_dynamic = is_dynamic;
  }

private:
  void clear() noexcept
  {
    if (m_kind == ValueKind_String)
    {
      delete m_value.s;
      m_value.s = nullptr;
    }
    else if (m_kind == ValueKind_List)
    {
      delete m_value.sv;
      m_value.sv = nullptr;
    }
    m_kind = ValueKind_None;
    m_value.i = 0;
  }

  void copy_from(const Argument &other)
  {
    m_kind = other.m_kind;
    m_is_dynamic = other.m_is_dynamic;
    switch (other.m_kind)
    {
    case ValueKind_None:
      m_value.i = 0;
      break;
    case ValueKind_Bool:
      m_value.b = other.m_value.b;
      break;
    case ValueKind_Int:
      m_value.i = other.m_value.i;
      break;
    case ValueKind_Double:
      m_value.d = other.m_value.d;
      break;
    case ValueKind_String:
      m_value.s = other.m_value.s ? new std::string(*other.m_value.s) : nullptr;
      break;
    case ValueKind_List:
      m_value.sv = other.m_value.sv
                       ? new std::vector<std::string>(*other.m_value.sv)
                       : nullptr;
      break;
    }
  }

  void move_from(Argument &other) noexcept
  {
    m_kind = other.m_kind;
    m_is_dynamic = other.m_is_dynamic;
    m_value = other.m_value;
    other.m_kind = ValueKind_None;
    other.m_is_dynamic = false;
    other.m_value.i = 0;
  }

  ValueKind m_kind;
  union
  {
    bool b;
    long long i;
    double d;
    std::string *s;
    std::vector<std::string> *sv;
  } m_value;
  bool m_is_dynamic;
};

// Helper struct for type-safe argument retrieval
template <typename T>
struct ArgGetter
{
  static const T *get(const Argument &)
  {
    return nullptr;
  }
};

template <>
struct ArgGetter<bool>
{
  static const bool *get(const Argument &v)
  {
    return v.get_bool();
  }
};

template <>
struct ArgGetter<long long>
{
  static const long long *get(const Argument &v)
  {
    return v.get_int();
  }
};

template <>
struct ArgGetter<double>
{
  static const double *get(const Argument &v)
  {
    return v.get_double();
  }
};

template <>
struct ArgGetter<std::string>
{
  static const std::string *get(const Argument &v)
  {
    return v.get_string();
  }
};

template <>
struct ArgGetter<std::vector<std::string>>
{
  static const std::vector<std::string> *get(const Argument &v)
  {
    return v.get_string_vector();
  }
};

using ArgumentMap = std::map<std::string, Argument>;

class Io
{
public:
  Io() = default;

  Io(const ArgumentMap &args,
     std::istream *in_stream = nullptr,
     std::ostream *out_stream = nullptr,
     std::ostream *err_stream = nullptr)
      : m_args(args), m_input_stream(in_stream), m_output_stream(out_stream), m_error_stream(err_stream)
  {
  }

  bool has(const std::string &key) const
  {
    return m_args.find(key) != m_args.end();
  }

  const Argument *find(const std::string &key) const
  {
    auto it = m_args.find(key);
    return it != m_args.end() ? &it->second : nullptr;
  }

  template <typename T>
  const T *get_if(const std::string &key) const
  {
    auto it = m_args.find(key);
    if (it == m_args.end())
      return nullptr;
    return ArgGetter<T>::get(it->second);
  }

  template <typename T>
  T get(const std::string &key)
  {
    const T *ptr = get_if<T>(key);
    if (!ptr)
    {
      throw std::runtime_error("argument '" + key + "' missing or wrong type");
    }
    return *ptr;
  }

  template <typename T>
  T get(const std::string &key, const T &default_value)
  {
    const T *ptr = get_if<T>(key);
    return ptr ? *ptr : default_value;
  }

  void print(const char *s) const
  {
    if (s)
    {
      output_stream().write(s, std::strlen(s));
    }
  }

  void println(const char *s) const
  {
    print(s);
    output_stream().put('\n');
  }

  void err(const char *e) const
  {
    if (e)
    {
      error_stream().write(e, std::strlen(e));
    }
  }

  void errln(const char *e) const
  {
    err(e);
    error_stream().put('\n');
  }

  void to_err(const std::stringstream &sstr)
  {
    errln(sstr.str().data());
  }

  template <typename T>
  void set_output(const std::string &key, const T &value)
  {
    m_output[key] = Argument(value);
  }

  void set_output(const ArgumentMap &output)
  {
    m_output = output;
  }

  const ArgumentMap &arguments() const
  {
    return m_args;
  }

  ArgumentMap dynamic_arguments() const
  {
    ArgumentMap dynamic_args;
    for (auto it = m_args.begin(); it != m_args.end(); ++it)
    {
      if (it->second.is_dynamic())
      {
        dynamic_args[it->first] = it->second;
      }
    }
    return dynamic_args;
  }

  const ArgumentMap &output() const
  {
    return m_output;
  }

  std::istream &input_stream() const
  {
    return m_input_stream != nullptr ? *m_input_stream : std::cin;
  }

  std::ostream &output_stream() const
  {
    return m_output_stream != nullptr ? *m_output_stream : std::cout;
  }

  std::ostream &error_stream() const
  {
    return m_error_stream != nullptr ? *m_error_stream : std::cerr;
  }

  bool is_redirecting_error() const
  {
    return m_error_stream != nullptr;
  }

  bool is_redirecting_input() const
  {
    return m_input_stream != nullptr;
  }

  bool is_redirecting_output() const
  {
    return m_output_stream != nullptr;
  }

  const ast::Node &get_object()
  {
    return m_object;
  }

  void set_object(const ast::Node &n)
  {
    m_object = n;
  }

  const size_t &content_len() const
  {
    return m_content_len;
  }

  virtual void set_content_len(const size_t content_len)
  {
    m_content_len = content_len;
  }

  // Signal that a long-running request (e.g. a streamed file upload/download)
  // is still making progress. No-op by default; overridden where there's a
  // liveness mechanism to feed (see MiddlewareContext::update_heartbeat).
  virtual void update_heartbeat() {}

protected:
  ArgumentMap m_args;

private:
  ArgumentMap m_output;
  std::istream *m_input_stream = nullptr;
  std::ostream *m_output_stream = nullptr;
  std::ostream *m_error_stream = nullptr;
  ast::Node m_object;
  size_t m_content_len = 0;
};

template <>
inline bool Io::get(const std::string &key)
{
  // Look for negative argument first in case there is a default assigned to the normal argument
  const auto *neg_ptr = get_if<bool>("no-" + key);
  if (neg_ptr)
  {
    return !*neg_ptr;
  }

  const auto *ptr = get_if<bool>(key);
  if (!ptr)
  {
    throw std::invalid_argument("argument '" + key + "' missing or wrong type");
  }
  return *ptr;
}

template <>
inline bool Io::get(const std::string &key, const bool &default_value)
{
  // Look for negative argument first in case there is a default assigned to the normal argument
  const auto *neg_ptr = get_if<bool>("no-" + key);
  if (neg_ptr)
  {
    return !*neg_ptr;
  }
  const auto *ptr = get_if<bool>(key);
  return ptr ? *ptr : default_value;
}

struct ContextArgs
{
  std::string command_path;
  ArgumentMap args;
  std::vector<std::string> passthrough_args;
  std::istream *in_stream;
  std::ostream *out_stream;
  std::ostream *err_stream;

  ContextArgs(const std::string &cmd_path,
              const ArgumentMap &arguments,
              const std::vector<std::string> &passthrough = std::vector<std::string>(),
              std::istream *input = nullptr,
              std::ostream *output = nullptr,
              std::ostream *error = nullptr)
      : command_path(cmd_path), args(arguments), passthrough_args(passthrough),
        in_stream(input), out_stream(output), err_stream(error)
  {
  }
};

class InvocationContext : public Io
{
public:
  explicit InvocationContext(const ContextArgs &context_args)
      : Io(context_args.args, context_args.in_stream, context_args.out_stream, context_args.err_stream),
        m_command_path(context_args.command_path),
        m_passthrough_args(context_args.passthrough_args)
  {
  }

  const std::string &command_path() const
  {
    return m_command_path;
  }

  // Check if passthrough arguments were provided (after --)
  bool has_passthrough() const
  {
    return !m_passthrough_args.empty();
  }

  // Get the passthrough arguments (arguments after -- delimiter)
  const std::vector<std::string> &get_passthrough_args() const
  {
    return m_passthrough_args;
  }

private:
  std::string m_command_path;
  std::vector<std::string> m_passthrough_args;
};

class CommandProviderImpl
{
public:
  struct CommandDefaultValue
  {
    enum ValueKind
    {
      ValueKind_None = 0,
      ValueKind_Bool = 1,
      ValueKind_Int = 2,
      ValueKind_Double = 3,
      ValueKind_String = 4
    };

    ValueKind kind;
    union
    {
      int bool_value;
      long long int_value;
      double double_value;
      const char *string_value;
    } value;

    CommandDefaultValue()
    {
      kind = ValueKind_None;
      value.int_value = 0;
    }

    explicit CommandDefaultValue(bool v)
    {
      kind = ValueKind_Bool;
      value.bool_value = v ? 1 : 0;
    }

    explicit CommandDefaultValue(long long v)
    {
      kind = ValueKind_Int;
      value.int_value = v;
    }

    explicit CommandDefaultValue(double v)
    {
      kind = ValueKind_Double;
      value.double_value = v;
    }

    explicit CommandDefaultValue(const char *v)
    {
      kind = ValueKind_String;
      value.string_value = v;
    }
  };

  class CommandRegistrationContext
  {
  public:
    enum ArgumentType
    {
      ArgumentType_Flag = 0,
      ArgumentType_Single = 1,
      ArgumentType_Multiple = 2,
    };

    typedef void *CommandHandle;
    typedef int (*CommandHandler)(InvocationContext &context);

    virtual ~CommandRegistrationContext()
    {
    }

    virtual CommandHandle create_command(const char *name, const char *help) = 0;
    virtual CommandHandle get_root_command() = 0;
    virtual void add_alias(CommandHandle command, const char *alias) = 0;
    virtual void add_keyword_arg(CommandHandle command,
                                 const char *name,
                                 const char **aliases,
                                 unsigned int aliasCount,
                                 const char *help,
                                 ArgumentType type,
                                 int required,
                                 const CommandDefaultValue *defaultValue) = 0;
    virtual void add_positional_arg(CommandHandle command,
                                    const char *name,
                                    const char *help,
                                    ArgumentType type,
                                    int required,
                                    const CommandDefaultValue *defaultValue) = 0;
    virtual void set_handler(CommandHandle command, CommandHandler handler) = 0;
    virtual void add_subcommand(CommandHandle parent, CommandHandle child) = 0;
    virtual void add_to_server(CommandHandle command) = 0;
  };

  virtual ~CommandProviderImpl()
  {
  }

  virtual void register_commands(CommandRegistrationContext &context) = 0;
};

typedef CommandProviderImpl::CommandDefaultValue DefaultValue;
typedef Factory<CommandProviderImpl> CommandProvider;

struct PluginMetadata
{
  PluginMetadata()
  {
  }

  PluginMetadata(const std::string &displayName,
                 const std::string &versionString,
                 const std::string &fileName)
      : display_name(displayName), version(versionString), filename(fileName)
  {
  }

  std::string display_name;
  std::string version;
  std::string filename;
};

class PluginManager
{
public:
  struct LoadedPlugin
  {
    LoadedPlugin()
        : handle(nullptr)
    {
    }

    LoadedPlugin(const PluginMetadata &pluginMetadata, void *pluginHandle)
        : metadata(pluginMetadata), handle(pluginHandle)
    {
    }

    PluginMetadata metadata;
    void *handle;
  };

  PluginManager();
  ~PluginManager();

  void register_command_provider(std::unique_ptr<CommandProvider> provider);

  // Capture metadata the active plug-in supplies during registration.
  void register_plugin_metadata(const char *display_name, const char *version);

  // Register commands from all providers, attaching them under the supplied root command.
  void register_commands(parser::Command &rootCommand);

  void load_plugins(const std::string &plugins_path);

  const std::string &get_plugins_path() const
  {
    return m_plugins_path;
  }

  const std::vector<LoadedPlugin> &get_loaded_plugins() const
  {
    return m_plugins;
  }

  const std::set<parser::command_ptr> &get_server_commands() const
  {
    return m_server_commands;
  }

  // Names of plug-in top-level commands refused during registration because they
  // would shadow a built-in verb or collide with another plug-in.
  const std::vector<std::string> &get_rejected_command_names() const
  {
    return m_rejected_command_names;
  }

  PluginManager(const PluginManager &) = delete;
  PluginManager &operator=(const PluginManager &) = delete;

private:
  void unload_plugins();
  void record_loaded_plugin(void *plugin_handle, const std::string &plugin_identifier);
  bool is_display_name_in_use(const std::string &name) const;
  void discard_command_providers_from(std::size_t start_index);
  void load_plugin_file(const std::string &plugin_path, const std::string &entry_name);

  std::vector<std::unique_ptr<CommandProvider>> m_command_providers;
  std::set<parser::command_ptr> m_server_commands;
  std::string m_plugins_path;
  std::vector<LoadedPlugin> m_plugins;
  PluginMetadata m_pending_metadata;
  bool m_metadata_pending;
  bool m_registration_rejected;
  std::string m_duplicate_display_name;
  std::size_t m_provider_snapshot;
  std::vector<std::string> m_rejected_command_names;
};

inline PluginManager::PluginManager()
    : m_metadata_pending(false),
      m_registration_rejected(false),
      m_provider_snapshot(0)
{
}

inline PluginManager::~PluginManager()
{
  unload_plugins();
}

inline void PluginManager::register_command_provider(std::unique_ptr<CommandProvider> provider)
{
  if (!provider || m_registration_rejected)
  {
    return;
  }

  m_command_providers.push_back(std::move(provider));
}

inline void PluginManager::register_plugin_metadata(const char *display_name, const char *version)
{
  if (m_registration_rejected)
  {
    return;
  }

  m_pending_metadata.display_name = display_name ? display_name : "";
  if (!m_pending_metadata.display_name.empty() && is_display_name_in_use(m_pending_metadata.display_name))
  {
    m_registration_rejected = true;
    m_duplicate_display_name = m_pending_metadata.display_name;
    m_metadata_pending = false;
    m_pending_metadata = PluginMetadata();
    return;
  }

  m_pending_metadata.version = version ? version : "";
  m_metadata_pending = true;
}

inline bool PluginManager::is_display_name_in_use(const std::string &name) const
{
  if (name.empty())
  {
    return false;
  }

  return std::any_of(m_plugins.begin(), m_plugins.end(), [&name](const auto &plugin)
                     { return plugin.metadata.display_name == name; });
}

inline void PluginManager::discard_command_providers_from(std::size_t start_index)
{
  if (start_index >= m_command_providers.size())
  {
    return;
  }

  m_command_providers.resize(start_index);
}
} // namespace plugin

#endif
