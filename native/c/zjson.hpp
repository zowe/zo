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

#ifndef ZJSON_HPP
#define ZJSON_HPP

#include <string>
#include <vector>
#include <map>
#include <functional>
#include <stdexcept>
#include <sstream>
#include <iomanip>
#include <cctype>
#include <cstdlib>
#include <cmath>
#include <limits>
#include <optional>
#include <variant>
#include <type_traits>
#include "zjsonm.h"
#include "zjsontype.h"
#include "zstd.hpp"
#include <hwtjic.h> // ensure to include /usr/include

/*
 * ZJson - C++ JSON library with automatic struct serialization
 *
 * ==================== COMPILER REQUIREMENTS ====================
 *
 * Requires ibm-clang (IBM Open XL C/C++) with C++17 support.
 *
 * ==================== QUICK REFERENCE ====================
 *
 * Parsing:
 *   zjson::Value data = zjson::from_str(json_string);
 *   auto result = zjson::from_str<MyStruct>(json_string);
 *
 * Serialization:
 *   std::string json = zjson::to_string(obj);
 *   std::string pretty = zjson::to_string_pretty(obj);
 *
 * Struct Registration:
 *   ZJSON_DERIVE(StructName, field1, field2, ...)
 *   ZJSON_SERIALIZABLE(StructName, ZJSON_FIELD(StructName, field).rename("name"))
 *
 * Dynamic JSON:
 *   zjson::Value obj = zjson::Value::create_object();
 *   zjson::Value arr = zjson::Value::create_array();
 *   obj["name"] = "John"; arr[0] = "item";
 *
 * Type Conversion:
 *   zjson::Value val = zjson::to_value(obj);
 *   auto obj = zjson::from_value<MyStruct>(val);
 *
 * Value Access:
 *   value["key"]              // Object field access
 *   value[0]                  // Array element access
 *   value.as_string()         // Type conversion
 *
 * Error Handling:
 *   auto result = zjson::from_str<MyStruct>(json);
 *   if (result.has_value()) { auto obj = result.value(); }
 *
 * Field Attributes:
 *   .rename("newName")        // JSON field name
 *   .skip()                   // Skip serialization/deserialization
 *   .skip_serializing_if_none()  // Skip if optional field is empty
 *   .with_default(func)       // Default value for missing fields
 *
 * Optional Fields:
 *   std::optional<T> field; // Nullable fields (null when empty)
 */

// Forward declarations
namespace zjson
{
class Value;
class Error;
template <typename T>
struct Serializable;
template <typename T>
struct Deserializable;
inline std::string value_to_json_string(const Value &value);

// Key/value representation for a JSON object.
//
// Ordered (std::map), not hashed: std::hash<std::string> resolves to the out-of-line libc++ helper
// std::__1_e::__hash_memory, which is not exported by CRTEQCXE on z/OS systems below the required
// Language Environment maintenance level and fails with CEE3561S at load time. See zowex#871 and
// doc/troubleshooting.md. Do not change this back to std::unordered_map without re-checking the
// imported symbol report produced by `make runtime-imports`.
//
// Deliberately not named `Object`: that name is already an enumerator of Value::Type and would
// shadow this alias inside every member of Value.
using ObjectMap = std::map<std::string, Value>;

// Attributes namespace with RenameAll implementation
namespace attributes
{
struct RenameAll
{
  enum CaseStyle
  {
    none, // No transformation (default)
    lowercase,
    UPPERCASE,
    PascalCase,
    camelCase,
    snake_case,
    SCREAMING_SNAKE_CASE,
    kebab_case,
    SCREAMING_KEBAB_CASE
  };

  static std::string transform_name(const std::string &name, CaseStyle style)
  {
    switch (style)
    {
    case none:
      return name; // No transformation
    case lowercase:
    {
      std::string result;
      for (char c : name)
      {
        result += std::tolower(c);
      }
      return result;
    }
    case UPPERCASE:
    {
      std::string result;
      for (char c : name)
      {
        result += std::toupper(c);
      }
      return result;
    }
    case snake_case:
      return name; // Already in snake_case format (C++ convention)
    case camelCase:
    {
      std::string result;
      bool capitalize_next = false;
      for (char c : name)
      {
        if (c == '_')
        {
          capitalize_next = true;
        }
        else if (capitalize_next)
        {
          result += std::toupper(c);
          capitalize_next = false;
        }
        else
        {
          result += std::tolower(c);
        }
      }
      return result;
    }
    case PascalCase:
    {
      std::string result = transform_name(name, camelCase);
      if (!result.empty())
      {
        result[0] = std::toupper(result[0]);
      }
      return result;
    }
    case SCREAMING_SNAKE_CASE:
    {
      std::string result;
      for (char c : name)
      {
        result += std::toupper(c);
      }
      return result;
    }
    case kebab_case:
    {
      std::string result;
      for (char c : name)
      {
        result += (c == '_') ? '-' : std::tolower(c);
      }
      return result;
    }
    case SCREAMING_KEBAB_CASE:
    {
      std::string result;
      for (char c : name)
      {
        result += (c == '_') ? '-' : std::toupper(c);
      }
      return result;
    }
    default:
      return name;
    }
  }
};
} // namespace attributes

// Compile-time container attributes
namespace detail
{
template <typename T>
struct RenameAllConfig
{
  static constexpr attributes::RenameAll::CaseStyle value = attributes::RenameAll::none;
};

template <typename T>
struct DenyUnknownFieldsConfig
{
  static constexpr bool value = false;
};

template <typename T>
struct StructConfig
{
  static constexpr attributes::RenameAll::CaseStyle rename_all_case = RenameAllConfig<T>::value;
  static constexpr bool deny_unknown_fields = DenyUnknownFieldsConfig<T>::value;
};

template <typename T>
struct is_optional : std::false_type
{
};

template <typename T>
struct is_optional<std::optional<T>> : std::true_type
{
};
} // namespace detail

/**
 * Error handling class for JSON operations
 */
class Error : public std::runtime_error
{
public:
  enum ErrorKind
  {
    InvalidValue,
    InvalidType,
    InvalidLength,
    InvalidData,
    MissingField,
    UnknownField,
    Custom
  };

private:
  ErrorKind kind_;
  size_t line_;
  size_t column_;

public:
  Error(ErrorKind kind, const std::string &message)
      : std::runtime_error(message), kind_(kind), line_(0), column_(0)
  {
  }

  Error(ErrorKind kind, const std::string &message, size_t line, size_t column)
      : std::runtime_error(message), kind_(kind), line_(line), column_(column)
  {
  }

  ErrorKind kind() const
  {
    return kind_;
  }
  size_t line() const
  {
    return line_;
  }
  size_t column() const
  {
    return column_;
  }

  static inline Error invalid_value(const std::string &msg)
  {
    return Error(InvalidValue, "Invalid value: " + msg);
  }

  static inline Error invalid_type(const std::string &expected, const std::string &found)
  {
    return Error(InvalidType, "Invalid type. Expected " + expected + ", found " + found);
  }

  static inline Error missing_field(const std::string &field)
  {
    return Error(MissingField, "Missing field: " + field);
  }

  static inline Error unknown_field(const std::string &field)
  {
    return Error(UnknownField, "Unknown field: " + field);
  }

  static inline Error invalid_data(const std::string &msg)
  {
    return Error(InvalidData, "Invalid data: " + msg);
  }
};

/**
 * Value type for JSON operations
 */
class Value
{
public:
  // Public methods for creating Values to avoid private member access
  static Value create_object()
  {
    Value result{};
    result.data_ = ObjectMap();
    return result;
  }

  static Value create_array()
  {
    Value result{};
    result.data_ = std::vector<Value>();
    return result;
  }

  void add_to_object(const std::string &key, const Value &value)
  {
    if (!is_object())
    {
      throw Error::invalid_type("object", "other");
    }
    get_object()[key] = value;
  }

  void add_to_array(const Value &value)
  {
    if (!is_array())
    {
      throw Error::invalid_type("array", "other");
    }
    get_array().push_back(value);
  }

  void reserve_array(size_t capacity)
  {
    if (!is_array())
    {
      throw Error::invalid_type("array", "other");
    }
    get_array().reserve(capacity);
  }
  enum Type
  {
    Null,
    Bool,
    Number,
    String,
    Array,
    Object
  };

private:
  // Variant type for JSON values
  using ValueVariant = std::variant<
      std::monostate,     // Null
      bool,               // Bool
      long long,          // Integer
      double,             // Float
      std::string,        // String
      std::vector<Value>, // Array
      ObjectMap           // Object
      >;

  ValueVariant data_;

  // Helper methods for variant access
  inline std::vector<Value> &get_array()
  {
    return std::get<std::vector<Value>>(data_);
  }
  inline ObjectMap &get_object()
  {
    return std::get<ObjectMap>(data_);
  }

  inline const bool &get_bool() const
  {
    return std::get<bool>(data_);
  }
  inline const long long &get_long_long() const
  {
    return std::get<long long>(data_);
  }
  inline const double &get_double() const
  {
    return std::get<double>(data_);
  }
  inline const std::string &get_string() const
  {
    return std::get<std::string>(data_);
  }
  inline const std::vector<Value> &get_array() const
  {
    return std::get<std::vector<Value>>(data_);
  }
  inline const ObjectMap &get_object() const
  {
    return std::get<ObjectMap>(data_);
  }

public:
  Value() = default;
  explicit Value(bool b)
      : data_(b)
  {
  }
  explicit Value(int i)
      : data_(static_cast<long long>(i))
  {
  }
  explicit Value(long long ll)
      : data_(ll)
  {
  }
  explicit Value(unsigned long long ull)
  {
    if (ull <= static_cast<unsigned long long>(std::numeric_limits<long long>::max()))
    {
      data_ = static_cast<long long>(ull);
    }
    else
    {
      data_ = static_cast<double>(ull);
    }
  }
  explicit Value(double d)
      : data_(d)
  {
  }
  explicit Value(const std::string &s)
      : data_(s)
  {
  }
  explicit Value(const char *s)
      : data_(std::string(s))
  {
  }

  // Copy constructor and assignment
  Value(const Value &other)
      : data_(other.data_)
  {
  }

  Value &operator=(const Value &other)
  {
    if (this != &other)
    {
      data_ = other.data_;
    }
    return *this;
  }

  ~Value()
  {
  }

  inline Type get_type() const
  {
    switch (data_.index())
    {
    case 0:
      return Null;
    case 1:
      return Bool;
    case 2:
    case 3:
      return Number;
    case 4:
      return String;
    case 5:
      return Array;
    case 6:
      return Object;
    default:
      return Null;
    }
  }

  inline bool as_bool() const
  {
    if (!is_bool())
      throw Error::invalid_type("bool", type_name());
    return get_bool();
  }

  inline long long as_int64() const
  {
    if (is_integer())
    {
      return get_long_long();
    }

    if (is_double())
    {
      double value = get_double();

      if (value != std::floor(value))
      {
        std::stringstream ss;
        ss << value;
        throw Error::invalid_value("Cannot convert floating-point number " + ss.str() + " to int64");
      }
      else if (isnan(value) || isinf(value))
      {
        std::stringstream ss;
        ss << value;
        throw Error::invalid_value("Number is not finite: " + ss.str());
      }
      else if (value > static_cast<double>(std::numeric_limits<long long>::max()) || value < static_cast<double>(std::numeric_limits<long long>::min()))
      {
        std::stringstream ss;
        ss << value;
        throw Error::invalid_value("Number " + ss.str() + " overflows long long range");
      }

      return static_cast<long long>(value);
    }

    throw Error::invalid_type("number", type_name());
  }

  inline unsigned long long as_uint64() const
  {
    if (is_integer())
    {
      long long value = get_long_long();
      if (value < 0)
      {
        std::stringstream ss;
        ss << value;
        throw Error::invalid_value("Cannot convert negative number " + ss.str() + " to uint64");
      }
      return static_cast<unsigned long long>(value);
    }

    if (is_double())
    {
      double value = get_double();

      if (value < 0)
      {
        std::stringstream ss;
        ss << value;
        throw Error::invalid_value("Cannot convert negative number " + ss.str() + " to uint64");
      }
      else if (value != std::floor(value))
      {
        std::stringstream ss;
        ss << value;
        throw Error::invalid_value("Cannot convert floating-point number " + ss.str() + " to uint64");
      }
      else if (isnan(value) || isinf(value))
      {
        std::stringstream ss;
        ss << value;
        throw Error::invalid_value("Number is not finite: " + ss.str());
      }
      else if (value > static_cast<double>(std::numeric_limits<unsigned long long>::max()))
      {
        std::stringstream ss;
        ss << value;
        throw Error::invalid_value("Number " + ss.str() + " overflows unsigned long long range");
      }

      return static_cast<unsigned long long>(value);
    }

    throw Error::invalid_type("number", type_name());
  }

  inline double as_double() const
  {
    if (is_double())
    {
      double value = get_double();
      if (isnan(value) || isinf(value))
      {
        std::stringstream ss;
        ss << value;
        throw Error::invalid_value("Number is not finite: " + ss.str());
      }
      return value;
    }

    if (is_integer())
    {
      return static_cast<double>(get_long_long());
    }

    throw Error::invalid_type("number", type_name());
  }

  inline std::string as_string() const
  {
    if (!is_string())
      throw Error::invalid_type("string", type_name());
    return get_string();
  }

  const std::vector<Value> &as_array() const
  {
    if (!is_array())
      throw Error::invalid_type("array", type_name());
    return get_array();
  }

  const ObjectMap &as_object() const
  {
    if (!is_object())
      throw Error::invalid_type("object", type_name());
    return get_object();
  }

  inline bool is_null() const
  {
    return std::holds_alternative<std::monostate>(data_);
  }
  inline bool is_bool() const
  {
    return std::holds_alternative<bool>(data_);
  }
  inline bool is_integer() const
  {
    return std::holds_alternative<long long>(data_);
  }
  inline bool is_double() const
  {
    return std::holds_alternative<double>(data_);
  }
  inline bool is_string() const
  {
    return std::holds_alternative<std::string>(data_);
  }
  inline bool is_array() const
  {
    return std::holds_alternative<std::vector<Value>>(data_);
  }
  inline bool is_object() const
  {
    return std::holds_alternative<ObjectMap>(data_);
  }

  // Object access by key
  Value &operator[](const std::string &key)
  {
    if (is_null())
    {
      // Convert null to empty object on first access
      data_ = ObjectMap();
    }

    if (!is_object())
    {
      throw Error::invalid_type("object", type_name());
    }

    return get_object()[key]; // Creates entry if doesn't exist
  }

  const Value &operator[](const std::string &key) const
  {
    if (!is_object())
    {
      throw Error::invalid_type("object", type_name());
    }

    const auto &obj = get_object();
    const auto it = obj.find(key);
    if (it == obj.end())
    {
      static const Value null_value; // Return reference to static null
      return null_value;
    }

    return it->second;
  }

  // Array access by index
  Value &operator[](size_t index)
  {
    if (is_null())
    {
      // Convert null to empty array on first access
      data_ = std::vector<Value>();
    }

    if (!is_array())
    {
      throw Error::invalid_type("array", type_name());
    }

    std::vector<Value> &arr = get_array();

    // Expand array if needed
    if (index >= arr.size())
    {
      arr.resize(index + 1);
    }

    return arr[index];
  }

  const Value &operator[](size_t index) const
  {
    if (!is_array())
    {
      throw Error::invalid_type("array", type_name());
    }

    const std::vector<Value> &arr = get_array();
    if (index >= arr.size())
    {
      static const Value null_value; // Return reference to static null
      return null_value;
    }

    return arr[index];
  }

private:
  inline std::string type_name() const
  {
    switch (get_type())
    {
    case Null:
      return "null";
    case Bool:
      return "bool";
    case Number:
      return "number";
    case String:
      return "string";
    case Array:
      return "array";
    case Object:
      return "object";
    default:
      return "unknown";
    }
  }
};

/**
 * Serialization trait for JSON operations
 */
template <typename T>
struct Serializable
{
  static constexpr bool value = false;

  static Value serialize(const T &obj)
  {
    static_assert(Serializable<T>::value, "Type must implement Serializable trait");
    return Value();
  }
};

/**
 * Deserialization trait for JSON operations
 */
template <typename T>
struct Deserializable
{
  static constexpr bool value = false;

  static zstd::expected<T, Error> deserialize(const Value &value)
  {
    static_assert(Deserializable<T>::value, "Type must implement Deserializable trait");
    return zstd::make_unexpected(Error::invalid_type("deserializable", "unknown"));
  }
};

// Specializations for basic types
template <>
struct Serializable<bool>
{
  static constexpr bool value = true;
  static Value serialize(const bool &obj) noexcept
  {
    return Value(obj);
  }
};

template <>
struct Deserializable<bool>
{
  static constexpr bool value = true;
  static zstd::expected<bool, Error> deserialize(const Value &value)
  {
    try
    {
      return value.as_bool();
    }
    catch (const Error &e)
    {
      return zstd::make_unexpected(e);
    }
  }
};

template <>
struct Serializable<int>
{
  static constexpr bool value = true;
  static Value serialize(const int &obj) noexcept
  {
    return Value(obj);
  }
};

template <>
struct Deserializable<int>
{
  static constexpr bool value = true;
  static zstd::expected<int, Error> deserialize(const Value &value)
  {
    try
    {
      // Get as int64 and downcast to int with range check
      long long val = value.as_int64();
      if (val > static_cast<long long>(std::numeric_limits<int>::max()) ||
          val < static_cast<long long>(std::numeric_limits<int>::min()))
      {
        std::stringstream ss;
        ss << val;
        throw Error::invalid_value("Number " + ss.str() + " overflows int range");
      }
      return static_cast<int>(val);
    }
    catch (const Error &e)
    {
      return zstd::make_unexpected(e);
    }
  }
};

template <>
struct Serializable<unsigned int>
{
  static constexpr bool value = true;
  static Value serialize(const unsigned int &obj) noexcept
  {
    return Value(static_cast<unsigned long long>(obj));
  }
};

template <>
struct Deserializable<unsigned int>
{
  static constexpr bool value = true;
  static zstd::expected<unsigned int, Error> deserialize(const Value &value)
  {
    try
    {
      // Get as uint64 and downcast to unsigned int with range check
      unsigned long long val = value.as_uint64();
      if (val > static_cast<unsigned long long>(std::numeric_limits<unsigned int>::max()))
      {
        std::stringstream ss;
        ss << val;
        throw Error::invalid_value("Number " + ss.str() + " overflows unsigned int range");
      }
      return static_cast<unsigned int>(val);
    }
    catch (const Error &e)
    {
      return zstd::make_unexpected(e);
    }
  }
};

template <>
struct Serializable<long long>
{
  static constexpr bool value = true;
  static Value serialize(const long long &obj) noexcept
  {
    return Value(obj);
  }
};

template <>
struct Deserializable<long long>
{
  static constexpr bool value = true;
  static zstd::expected<long long, Error> deserialize(const Value &value)
  {
    try
    {
      return value.as_int64();
    }
    catch (const Error &e)
    {
      return zstd::make_unexpected(e);
    }
  }
};

template <>
struct Serializable<unsigned long long>
{
  static constexpr bool value = true;
  static Value serialize(const unsigned long long &obj) noexcept
  {
    return Value(obj);
  }
};

template <>
struct Deserializable<unsigned long long>
{
  static constexpr bool value = true;
  static zstd::expected<unsigned long long, Error> deserialize(const Value &value)
  {
    try
    {
      return value.as_uint64();
    }
    catch (const Error &e)
    {
      return zstd::make_unexpected(e);
    }
  }
};

// Support for size_t (typically an alias for unsigned long long or unsigned int)
template <>
struct Serializable<size_t>
{
  static constexpr bool value = true;
  static Value serialize(const size_t &obj) noexcept
  {
    return Value(static_cast<unsigned long long>(obj));
  }
};

template <>
struct Deserializable<size_t>
{
  static constexpr bool value = true;
  static zstd::expected<size_t, Error> deserialize(const Value &value)
  {
    try
    {
      unsigned long long val = value.as_uint64();
      // size_t range check (in case size_t is smaller than unsigned long long)
      if (val > static_cast<unsigned long long>(std::numeric_limits<size_t>::max()))
      {
        std::stringstream ss;
        ss << val;
        throw Error::invalid_value("Number " + ss.str() + " overflows size_t range");
      }
      return static_cast<size_t>(val);
    }
    catch (const Error &e)
    {
      return zstd::make_unexpected(e);
    }
  }
};

template <>
struct Serializable<double>
{
  static constexpr bool value = true;
  static Value serialize(const double &obj) noexcept
  {
    return Value(obj);
  }
};

template <>
struct Deserializable<double>
{
  static constexpr bool value = true;
  static zstd::expected<double, Error> deserialize(const Value &value)
  {
    try
    {
      return value.as_double();
    }
    catch (const Error &e)
    {
      return zstd::make_unexpected(e);
    }
  }
};

template <>
struct Serializable<std::string>
{
  static constexpr bool value = true;
  static Value serialize(const std::string &obj) noexcept
  {
    return Value(obj);
  }
};

template <>
struct Deserializable<std::string>
{
  static constexpr bool value = true;
  static zstd::expected<std::string, Error> deserialize(const Value &value)
  {
    try
    {
      return value.as_string();
    }
    catch (const Error &e)
    {
      return zstd::make_unexpected(e);
    }
  }
};

// Specializations for Value itself - enables both to_value<Value> and from_str<Value>
template <>
struct Serializable<Value>
{
  static constexpr bool value = true;
  static Value serialize(const Value &obj)
  {
    return obj; // Value serializes to itself (identity)
  }
};

template <>
struct Deserializable<Value>
{
  static constexpr bool value = true;
  static zstd::expected<Value, Error> deserialize(const Value &value)
  {
    // Value to Value is just a copy - already parsed from JSON
    return value;
  }
};

// Specializations for std::vector<T>
template <typename T>
struct Serializable<std::vector<T>>
{
  static constexpr bool value = Serializable<T>::value;
  static Value serialize(const std::vector<T> &vec)
  {
    Value result = Value::create_array();
    result.reserve_array(vec.size());

    for (const auto &item : vec)
    {
      result.add_to_array(Serializable<T>::serialize(item));
    }
    return result;
  }
};

template <typename T>
struct Deserializable<std::vector<T>>
{
  static constexpr bool value = Deserializable<T>::value;
  static zstd::expected<std::vector<T>, Error> deserialize(const Value &value)
  {
    if (!value.is_array())
    {
      return zstd::make_unexpected(Error::invalid_type("array", "other"));
    }

    std::vector<T> result;
    const std::vector<Value> &array = value.as_array();
    result.reserve(array.size());

    for (const auto &element : array)
    {
      zstd::expected<T, Error> item_result = Deserializable<T>::deserialize(element);
      if (!item_result.has_value())
      {
        return zstd::make_unexpected(item_result.error());
      }
      result.push_back(item_result.value());
    }

    return result;
  }
};

// Specializations for std::optional<T>
template <typename T>
struct Serializable<std::optional<T>>
{
  static constexpr bool value = Serializable<T>::value;
  static Value serialize(const std::optional<T> &opt)
  {
    if (opt.has_value())
    {
      return Serializable<T>::serialize(opt.value());
    }
    else
    {
      return Value(); // Create null value
    }
  }
};

template <typename T>
struct Deserializable<std::optional<T>>
{
  static constexpr bool value = Deserializable<T>::value;
  static zstd::expected<std::optional<T>, Error> deserialize(const Value &value)
  {
    if (value.is_null())
    {
      return std::optional<T>(); // Empty optional
    }
    else
    {
      zstd::expected<T, Error> result = Deserializable<T>::deserialize(value);
      if (!result.has_value())
      {
        return zstd::make_unexpected(result.error());
      }
      return std::optional<T>(result.value());
    }
  }
};

/**
 * Field descriptor for reflection-like behavior
 */
template <typename T, typename FieldType>
struct Field
{
  std::string name;
  FieldType T::*member;
  bool skip_serializing;
  bool skip_deserializing;
  bool skip_if_none;  // For optional fields - skip if no value
  bool flatten_field; // For struct flattening - inline fields from nested struct
  std::string rename_to;
  std::function<FieldType()> default_value;

  Field(const std::string &n, FieldType T::*m)
      : name(n), member(m), skip_serializing(false), skip_deserializing(false), skip_if_none(false), flatten_field(false)
  {
  }

  inline Field &rename(const std::string &new_name)
  {
    rename_to = new_name;
    return *this;
  }

  inline Field &skip()
  {
    skip_serializing = true;
    skip_deserializing = true;
    return *this;
  }

  inline Field &skip_serializing_field()
  {
    skip_serializing = true;
    return *this;
  }

  inline Field &skip_deserializing_field()
  {
    skip_deserializing = true;
    return *this;
  }

  inline Field &with_default(std::function<FieldType()> default_fn)
  {
    default_value = default_fn;
    return *this;
  }

  inline Field &skip_serializing_if_none()
  {
    skip_if_none = true;
    return *this;
  }

  inline Field &flatten()
  {
    flatten_field = true;
    return *this;
  }

  inline std::string get_serialized_name() const
  {
    if (!rename_to.empty())
    {
      return rename_to;
    }
    if constexpr (detail::StructConfig<T>::rename_all_case == attributes::RenameAll::none)
    {
      return name;
    }
    else
    {
      return attributes::RenameAll::transform_name(name, detail::StructConfig<T>::rename_all_case);
    }
  }
};

/**
 * Main serialization and deserialization functions
 */

// to_string function for JSON serialization
template <typename T>
zstd::expected<std::string, Error> to_string(const T &value)
{
  static_assert(Serializable<T>::value, "Type must implement Serializable trait");
  try
  {
    Value serialized = Serializable<T>::serialize(value);
    return value_to_json_string(serialized);
  }
  catch (const Error &e)
  {
    return zstd::make_unexpected(e);
  }
  catch (const std::exception &e)
  {
    return zstd::make_unexpected(Error(Error::Custom, e.what()));
  }
}

// Helper function for JSON string escaping
inline std::string escape_json_string(const std::string &input)
{
  std::string output;
  output.reserve(input.length());

  for (char c : input)
  {
    switch (c)
    {
    case '"':
      output += "\\\"";
      break;
    case '\\':
      output += "\\\\";
      break;
    case '\b':
      output += "\\b";
      break;
    case '\f':
      output += "\\f";
      break;
    case '\n':
      output += "\\n";
      break;
    case '\r':
      output += "\\r";
      break;
    case '\t':
      output += "\\t";
      break;
    default:
      if (iscntrl(static_cast<unsigned char>(c)))
      {
        // Native control byte values do not correspond to the same Unicode code
        // points on this EBCDIC platform (e.g. IBM-1047 NL 0x15 vs Unicode NAK
        // U+0015), so echoing the byte's numeric value via \u00XX would misrepresent
        // it to a spec-compliant JSON reader. Emit the Unicode replacement character
        // instead of a specific, likely-wrong code point.
        output += "\\ufffd";
      }
      else
      {
        output += c;
      }
      break;
    }
  }
  return output;
}

// Helper function for JSON string unescaping
inline std::string unescape_json_string(const std::string &s)
{
  std::string res;
  res.reserve(s.length());
  for (std::string::size_type i = 0; i < s.length(); ++i)
  {
    if (s[i] == '\\' && i + 1 < s.length())
    {
      switch (s[++i])
      {
      case '"':
        res += '"';
        break;
      case '\\':
        res += '\\';
        break;
      case '/':
        res += '/';
        break;
      case 'b':
        res += '\b';
        break;
      case 'f':
        res += '\f';
        break;
      case 'n':
        res += '\n';
        break;
      case 'r':
        res += '\r';
        break;
      case 't':
        res += '\t';
        break;
      case 'u':
        if (i + 4 < s.length())
        {
          std::string hex = s.substr(i + 1, 4);
          char *endptr;
          unsigned long val = std::strtoul(hex.c_str(), &endptr, 16);
          bool valid_hex = endptr == hex.c_str() + 4;
          if (valid_hex && val < 256 && iscntrl(static_cast<unsigned char>(val)))
          {
            // \n, \t, \r, \b, \f have named escapes and are handled above, where
            // they are deliberately mapped to this platform's native control byte
            // rather than the Unicode code point. A \u00XX escape for the same code
            // point (named or not) has no such platform-specific mapping to fall
            // back on: e.g. U+0015 is Unicode NAK, but on this EBCDIC platform (IBM-1047)
            // byte 0x15 is NL, not NAK, so there is no reliable value to substitute -
            // reject outright rather than guess.
            throw Error::invalid_value("JSON string contains a disallowed control character escape: \\u" + hex);
          }
          else if (valid_hex && val < 256)
          {
            res += static_cast<char>(val);
          }
          else
          {
            // Either the hex digits themselves are malformed (should already
            // be rejected as invalid JSON before this text is reached), or the
            // escape names a code point above the single-byte range (U+0100
            // and above, including either half of a surrogate pair). This
            // decoder only ever produces single native bytes, so there is no
            // correct value to substitute here - reject rather than silently
            // keeping the escape text as if it had been decoded successfully.
            throw Error::invalid_value("JSON string contains an unsupported \\u escape: \\u" + hex);
          }
          i += 4;
        }
        else
        {
          res += '\\';
          res += 'u';
        }
        break;
      default:
        // Unrecognized escape sequence, just append as is
        res += '\\';
        res += s[i];
        break;
      }
    }
    else
    {
      res += s[i];
    }
  }
  return res;
}

// Maximum allowed JSON nesting depth to prevent stack overflow
static constexpr int MAX_JSON_DEPTH = 64;

// Note: this function intentionally does not catch its own exceptions. Any
// failure - a failed HWTJ call, a malformed number, a disallowed control
// character, exceeding MAX_JSON_DEPTH - propagates all the way to from_str's
// try/catch, which normalizes it into a zstd::expected failure and performs
// cleanup (ZJSMTERM). A single malformed field fails the whole parse rather
// than being silently dropped, matching how from_value<T> already treats
// errors one layer up (e.g. numeric overflow).
inline Value json_handle_to_value(JSON_INSTANCE *instance, KEY_HANDLE *key_handle, int depth = 0)
{
  // Check depth limit to prevent unbounded recursion
  if (depth >= MAX_JSON_DEPTH)
  {
    throw Error(Error::Custom, "JSON exceeds maximum nesting depth of " + std::to_string(MAX_JSON_DEPTH));
  }

  int type = 0;
  int rc = ZJSNGJST(instance, key_handle, &type);
  if (rc != 0)
  {
    throw Error(Error::Custom, "Failed to determine JSON value type");
  }

  switch (type)
  {
  case HWTJ_NULL_TYPE:
    return Value();

  case HWTJ_BOOLEAN_TYPE:
  {
    char bool_val = 0;
    rc = ZJSMGBOV(instance, key_handle, &bool_val);
    if (rc != 0)
    {
      throw Error(Error::Custom, "Failed to read JSON boolean value");
    }
    return Value(bool_val == HWTJ_TRUE);
  }

  case HWTJ_NUMBER_TYPE:
  {
    char *value_ptr = 0;
    int value_length = 0;
    rc = ZJSMGVAL(instance, key_handle, &value_ptr, &value_length);
    if (rc != 0)
    {
      throw Error(Error::Custom, "Failed to read JSON number value");
    }
    std::string str_val(value_ptr, value_length);
    if (str_val.find('.') != std::string::npos || str_val.find('e') != std::string::npos || str_val.find('E') != std::string::npos)
    {
      char *endptr = nullptr;
      double num_val = std::strtod(str_val.c_str(), &endptr);
      if (endptr == str_val.c_str() || *endptr != '\0')
      {
        throw Error::invalid_value("Malformed JSON number: " + str_val);
      }
      return Value(num_val);
    }
    else
    {
      char *endptr = nullptr;
      long long int_val = std::strtoll(str_val.c_str(), &endptr, 10);
      if (endptr == str_val.c_str() || *endptr != '\0')
      {
        throw Error::invalid_value("Malformed JSON number: " + str_val);
      }
      return Value(int_val);
    }
  }

  case HWTJ_STRING_TYPE:
  {
    char *value_ptr = 0;
    int value_length = 0;
    rc = ZJSMGVAL(instance, key_handle, &value_ptr, &value_length);
    if (rc != 0)
    {
      throw Error(Error::Custom, "Failed to read JSON string value");
    }
    std::string raw_str(value_ptr, value_length);
    return Value(unescape_json_string(raw_str));
  }

  case HWTJ_ARRAY_TYPE:
  {
    Value result = Value::create_array();

    int num_entries = 0;
    rc = ZJSMGNUE(instance, key_handle, &num_entries);
    if (rc != 0)
    {
      throw Error(Error::Custom, "Failed to determine JSON array size");
    }

    for (int i = 0; i < num_entries; i++)
    {
      KEY_HANDLE element_handle = {0};
      rc = ZJSMGAEN(instance, key_handle, &i, &element_handle);
      if (rc != 0)
      {
        throw Error(Error::Custom, "Failed to read JSON array element at index " + std::to_string(i));
      }
      Value element = json_handle_to_value(instance, &element_handle, depth + 1);
      result.add_to_array(element);
    }
    return result;
  }

  case HWTJ_OBJECT_TYPE:
  {
    Value result = Value::create_object();

    int num_entries = 0;
    rc = ZJSMGNUE(instance, key_handle, &num_entries);
    if (rc != 0)
    {
      throw Error(Error::Custom, "Failed to determine JSON object size");
    }

    for (int i = 0; i < num_entries; i++)
    {
      char key_buffer[256] = {0};
      char *key_buffer_ptr = key_buffer;
      int key_buffer_length = sizeof(key_buffer);
      KEY_HANDLE value_handle = {0};
      int actual_length = 0;
      std::vector<char> dynamic_buffer;

      rc = ZJSMGOEN(instance, key_handle, &i, &key_buffer_ptr, &key_buffer_length, &value_handle, &actual_length);
      if (rc == HWTJ_BUFFER_TOO_SMALL)
      {
        // Allocate larger buffer for long keys
        dynamic_buffer.resize(actual_length);
        key_buffer_ptr = &dynamic_buffer[0];
        key_buffer_length = actual_length;

        rc = ZJSMGOEN(instance, key_handle, &i, &key_buffer_ptr, &key_buffer_length, &value_handle, &actual_length);
      }

      if (rc != 0)
      {
        throw Error(Error::Custom, "Failed to read JSON object field at index " + std::to_string(i));
      }

      std::string key_name(key_buffer_ptr, actual_length);
      Value value = json_handle_to_value(instance, &value_handle, depth + 1);
      result.add_to_object(key_name, value);
    }
    return result;
  }

  default:
    throw Error(Error::Custom, "Unknown JSON value type encountered during parsing");
  }
}

// from_value function - convert Value to any deserializable type
template <typename T>
zstd::expected<T, Error> from_value(const Value &value)
{
  static_assert(Deserializable<T>::value, "Type must implement Deserializable trait");
  try
  {
    return Deserializable<T>::deserialize(value);
  }
  catch (const Error &e)
  {
    return zstd::make_unexpected(e);
  }
  catch (const std::exception &e)
  {
    return zstd::make_unexpected(Error(Error::Custom, e.what()));
  }
}

// to_value function - convert any serializable type to Value
template <typename T>
zstd::expected<Value, Error> to_value(const T &obj)
{
  static_assert(Serializable<T>::value, "Type must implement Serializable trait");
  try
  {
    return Serializable<T>::serialize(obj);
  }
  catch (const Error &e)
  {
    return zstd::make_unexpected(e);
  }
  catch (const std::exception &e)
  {
    return zstd::make_unexpected(Error(Error::Custom, e.what()));
  }
}

// from_str function for JSON deserialization
template <typename T>
zstd::expected<T, Error> from_str(const std::string &json_str)
{
  JSON_INSTANCE instance{};
  int rc = ZJSMINIT(&instance);
  if (rc != 0)
  {
    return zstd::make_unexpected(Error(Error::Custom, "Failed to initialize JSON parser"));
  }

  try
  {
    rc = ZJSMPARS(&instance, json_str.c_str());
    if (rc != 0)
    {
      ZJSMTERM(&instance);
      return zstd::make_unexpected(Error(Error::Custom, "Failed to parse JSON string"));
    }

    // Get root handle and convert to Value
    KEY_HANDLE root_handle{};
    Value parsed = json_handle_to_value(&instance, &root_handle);

    // Clean up
    ZJSMTERM(&instance);

    return from_value<T>(parsed);
  }
  catch (const Error &e)
  {
    ZJSMTERM(&instance);
    return zstd::make_unexpected(e);
  }
  catch (const std::exception &e)
  {
    ZJSMTERM(&instance);
    return zstd::make_unexpected(Error(Error::Custom, e.what()));
  }
}

// Convenience overload: from_str without template parameter defaults to Value
inline zstd::expected<Value, Error> from_str(const std::string &json_str)
{
  return from_str<Value>(json_str);
}

// Helper function to add indentation to JSON string
inline std::string add_json_indentation(const std::string &json_str, int spaces)
{
  std::string result;
  int indent_level = 0;
  bool in_string = false;
  bool escape_next = false;

  for (char ch : json_str)
  {
    if (escape_next)
    {
      result += ch;
      escape_next = false;
      continue;
    }

    if (ch == '\\' && in_string)
    {
      result += ch;
      escape_next = true;
      continue;
    }

    if (ch == '\"')
    {
      result += ch;
      in_string = !in_string;
    }
    else if (!in_string)
    {
      switch (ch)
      {
      case '{':
      case '[':
        result += ch;
        result += '\n';
        indent_level++;
        result.append(indent_level * spaces, ' ');
        break;
      case '}':
      case ']':
        result += '\n';
        indent_level--;
        result.append(indent_level * spaces, ' ');
        result += ch;
        break;
      case ',':
        result += ch;
        result += '\n';
        result.append(indent_level * spaces, ' ');
        break;
      case ':':
        result += ch;
        result += ' ';
        break;
      default:
        result += ch;
        break;
      }
    }
    else
    {
      result += ch;
    }
  }
  return result;
}

// to_string_pretty function for pretty-printed JSON serialization
template <typename T>
zstd::expected<std::string, Error> to_string_pretty(const T &value)
{
  zstd::expected<std::string, Error> result = to_string(value);
  if (!result.has_value())
  {
    return result;
  }

  try
  {
    // Use our simple indentation function instead of ZJson
    return add_json_indentation(result.value(), 2);
  }
  catch (const std::exception &e)
  {
    return zstd::make_unexpected(Error(Error::Custom, e.what()));
  }
}

// Helper function to add Value to JSON instance using ZJSM API
inline int value_to_json_instance(JSON_INSTANCE *instance, KEY_HANDLE *parent_handle, const std::string &entry_name, const Value &value)
{
  int rc = 0;
  int entry_type = 0;
  KEY_HANDLE new_entry_handle{};
  const char *entry_name_ptr = entry_name.empty() ? nullptr : entry_name.c_str();

  switch (value.get_type())
  {
  case Value::Null:
    entry_type = HWTJ_NULLVALUETYPE;
    rc = ZJSMCREN(instance, parent_handle, entry_name_ptr, nullptr, &entry_type, &new_entry_handle);
    break;

  case Value::Bool:
    entry_type = value.as_bool() ? HWTJ_TRUEVALUETYPE : HWTJ_FALSEVALUETYPE;
    rc = ZJSMCREN(instance, parent_handle, entry_name_ptr, nullptr, &entry_type, &new_entry_handle);
    break;

  case Value::Number:
  {
    entry_type = HWTJ_NUMVALUETYPE;
    std::stringstream ss;

    if (value.is_integer())
    {
      ss << value.as_int64();
    }
    else
    {
      // Use full double precision (17 significant digits)
      ss << std::setprecision(17) << value.as_double();
    }

    std::string num_str = ss.str();
    rc = ZJSMCREN(instance, parent_handle, entry_name_ptr, num_str.c_str(), &entry_type, &new_entry_handle);
    break;
  }

  case Value::String:
  {
    if (!value.as_string().empty())
    {
      entry_type = HWTJ_STRINGVALUETYPE;
      std::string escaped_str = escape_json_string(value.as_string());
      rc = ZJSMCREN(instance, parent_handle, entry_name_ptr, escaped_str.c_str(), &entry_type, &new_entry_handle);
    }
    else
    {
      // Empty strings must use HWTJ_JSONTEXTVALUETYPE because HWTJ_STRINGVALUETYPE doesn't support them
      entry_type = HWTJ_JSONTEXTVALUETYPE;
      rc = ZJSMCREN(instance, parent_handle, entry_name_ptr, "\"\"", &entry_type, &new_entry_handle);
    }
  }
  break;

  case Value::Array:
  {
    entry_type = HWTJ_JSONTEXTVALUETYPE;
    rc = ZJSMCREN(instance, parent_handle, entry_name_ptr, "[]", &entry_type, &new_entry_handle);
    if (rc != 0)
      break;

    // Add all array elements
    for (const auto &element : value.as_array())
    {
      rc = value_to_json_instance(instance, &new_entry_handle, "", element);
      if (rc != 0)
        break;
    }
    break;
  }

  case Value::Object:
  {
    entry_type = HWTJ_JSONTEXTVALUETYPE;
    rc = ZJSMCREN(instance, parent_handle, entry_name_ptr, "{}", &entry_type, &new_entry_handle);
    if (rc != 0)
      break;

    // Add all object properties
    for (const auto &[key, val] : value.as_object())
    {
      rc = value_to_json_instance(instance, &new_entry_handle, key, val);
      if (rc != 0)
        break;
    }
    break;
  }

  default:
    entry_type = HWTJ_NULLVALUETYPE;
    rc = ZJSMCREN(instance, parent_handle, entry_name_ptr, nullptr, &entry_type, &new_entry_handle);
    break;
  }

  return rc;
}

// Convert Value to JSON string using ZJSM API
inline std::string value_to_json_string(const Value &value)
{
  JSON_INSTANCE instance{};
  int rc = ZJSMINIT(&instance);
  if (rc != 0)
  {
    std::stringstream ss;
    ss << std::hex << rc;
    throw Error(Error::Custom, "Failed to initialize JSON parser. RC: x'" + ss.str() + "'");
  }

  // Start with an empty root object/array/value
  KEY_HANDLE root_handle{};

  try
  {
    // For root level values, we need to handle them specially
    if (value.is_object() || value.is_array())
    {
      // Parse a minimal JSON to get a root container
      const char *init_json = value.is_object() ? "{}" : "[]";
      rc = ZJSMPARS(&instance, init_json);
      if (rc != 0)
      {
        ZJSMTERM(&instance);
        std::stringstream ss;
        ss << std::hex << rc;
        throw Error(Error::Custom, "Failed to parse initial JSON container. RC: x'" + ss.str() + "'");
      }

      // Clear the initial content and rebuild
      if (value.is_object())
      {
        for (const auto &[key, val] : value.as_object())
        {
          rc = value_to_json_instance(&instance, &root_handle, key, val);
          if (rc != 0)
          {
            ZJSMTERM(&instance);
            std::stringstream ss;
            ss << std::hex << rc;
            throw Error(Error::Custom, "Failed to serialize object property '" + key + "'. RC: x'" + ss.str() + "'");
          }
        }
      }
      else // Array
      {
        const std::vector<Value> &arr = value.as_array();
        for (size_t i = 0; i < arr.size(); ++i)
        {
          rc = value_to_json_instance(&instance, &root_handle, "", arr[i]);
          if (rc != 0)
          {
            ZJSMTERM(&instance);
            std::stringstream ss;
            ss << std::hex << rc;
            std::stringstream idx_ss;
            idx_ss << i;
            throw Error(Error::Custom, "Failed to serialize array element at index " + idx_ss.str() + ". RC: x'" + ss.str() + "'");
          }
        }
      }
    }
    else
    {
      throw Error(Error::Custom, "Serializing primitive values is not supported");
    }

    // Serialize the result using the same pattern as ZJson
    char buffer[1] = {0};
    int buffer_length = (int)sizeof(buffer);
    int actual_length = 0;

    rc = ZJSMSERI(&instance, buffer, &buffer_length, &actual_length);

    std::string result;
    if (rc == HWTJ_BUFFER_TOO_SMALL)
    {
      // Allocate proper buffer and retry
      std::vector<char> dynamic_buffer(actual_length);
      int dynamic_buffer_length = actual_length;
      rc = ZJSMSERI(&instance, &dynamic_buffer[0], &dynamic_buffer_length, &actual_length);
      if (rc != 0)
      {
        ZJSMTERM(&instance);
        std::stringstream ss;
        ss << std::hex << rc;
        throw Error(Error::Custom, "Failed to serialize JSON with dynamic buffer. RC: x'" + ss.str() + "'");
      }
      result = std::string(&dynamic_buffer[0], actual_length);
    }
    else if (rc == 0)
    {
      result = std::string(buffer, actual_length);
    }
    else
    {
      ZJSMTERM(&instance);
      std::stringstream ss;
      ss << std::hex << rc;
      throw Error(Error::Custom, "Failed to serialize JSON. RC: x'" + ss.str() + "'");
    }

    ZJSMTERM(&instance);

    if (result.empty())
    {
      throw Error(Error::Custom, "Serialization resulted in empty string");
    }

    return result;
  }
  catch (const Error &e)
  {
    // Re-throw zjson::Error as-is
    throw;
  }
  catch (const std::exception &e)
  {
    ZJSMTERM(&instance);
    throw Error(Error::Custom, "Standard exception during serialization: " + std::string(e.what()));
  }
  catch (...)
  {
    ZJSMTERM(&instance);
    throw Error(Error::Custom, "Unknown exception during JSON serialization");
  }
}

// JSON parser using zjsonm C API
inline Value parse_json_string(const std::string &json_str)
{
  JSON_INSTANCE instance{};
  int rc = ZJSMINIT(&instance);
  if (rc != 0)
  {
    std::stringstream ss;
    ss << std::hex << rc;
    throw Error(Error::Custom, "Failed to initialize JSON parser for parsing. RC: x'" + ss.str() + "'");
  }

  try
  {
    rc = ZJSMPARS(&instance, json_str.c_str());
    if (rc != 0)
    {
      ZJSMTERM(&instance);
      std::stringstream ss;
      ss << std::hex << rc;
      throw Error(Error::Custom, "Failed to parse JSON string. RC: x'" + ss.str() + "'");
    }

    // Get root handle and convert to Value
    KEY_HANDLE root_handle = {0};
    Value result = json_handle_to_value(&instance, &root_handle);

    // Clean up
    ZJSMTERM(&instance);

    return result;
  }
  catch (const Error &e)
  {
    // Re-throw zjson::Error as-is
    throw;
  }
  catch (const std::exception &e)
  {
    ZJSMTERM(&instance);
    throw Error(Error::Custom, "Standard exception during JSON parsing: " + std::string(e.what()));
  }
  catch (...)
  {
    ZJSMTERM(&instance);
    throw Error(Error::Custom, "Unknown exception during JSON parsing");
  }
}

/**
 * Macro system for automatic serialization
 */

// Field creation macro
#define ZJSON_FIELD(StructType, field_name) \
  zjson::Field<StructType, decltype(StructType::field_name)>(#field_name, &StructType::field_name)

// Auto-generated field creation (with inline expansion)
#define ZJSON_FIELD_AUTO(StructType, field) \
  zjson::Field<StructType, decltype(StructType::field)>(#field, &StructType::field)

// Transform macros for field lists - these create Field objects from field names
#define ZJSON_TRANSFORM_FIELDS_1(StructType, f1) \
  ZJSON_FIELD_AUTO(StructType, f1)

#define ZJSON_TRANSFORM_FIELDS_2(StructType, f1, f2) \
  ZJSON_TRANSFORM_FIELDS_1(StructType, f1), ZJSON_FIELD_AUTO(StructType, f2)

#define ZJSON_TRANSFORM_FIELDS_3(StructType, f1, f2, f3) \
  ZJSON_TRANSFORM_FIELDS_2(StructType, f1, f2), ZJSON_FIELD_AUTO(StructType, f3)

#define ZJSON_TRANSFORM_FIELDS_4(StructType, f1, f2, f3, f4) \
  ZJSON_TRANSFORM_FIELDS_3(StructType, f1, f2, f3), ZJSON_FIELD_AUTO(StructType, f4)

#define ZJSON_TRANSFORM_FIELDS_5(StructType, f1, f2, f3, f4, f5) \
  ZJSON_TRANSFORM_FIELDS_4(StructType, f1, f2, f3, f4), ZJSON_FIELD_AUTO(StructType, f5)

#define ZJSON_TRANSFORM_FIELDS_6(StructType, f1, f2, f3, f4, f5, f6) \
  ZJSON_TRANSFORM_FIELDS_5(StructType, f1, f2, f3, f4, f5), ZJSON_FIELD_AUTO(StructType, f6)

#define ZJSON_TRANSFORM_FIELDS_7(StructType, f1, f2, f3, f4, f5, f6, f7) \
  ZJSON_TRANSFORM_FIELDS_6(StructType, f1, f2, f3, f4, f5, f6), ZJSON_FIELD_AUTO(StructType, f7)

#define ZJSON_TRANSFORM_FIELDS_8(StructType, f1, f2, f3, f4, f5, f6, f7, f8) \
  ZJSON_TRANSFORM_FIELDS_7(StructType, f1, f2, f3, f4, f5, f6, f7), ZJSON_FIELD_AUTO(StructType, f8)

#define ZJSON_TRANSFORM_FIELDS_9(StructType, f1, f2, f3, f4, f5, f6, f7, f8, f9) \
  ZJSON_TRANSFORM_FIELDS_8(StructType, f1, f2, f3, f4, f5, f6, f7, f8), ZJSON_FIELD_AUTO(StructType, f9)

#define ZJSON_TRANSFORM_FIELDS_10(StructType, f1, f2, f3, f4, f5, f6, f7, f8, f9, f10) \
  ZJSON_TRANSFORM_FIELDS_9(StructType, f1, f2, f3, f4, f5, f6, f7, f8, f9), ZJSON_FIELD_AUTO(StructType, f10)

#define ZJSON_TRANSFORM_FIELDS_11(StructType, f1, f2, f3, f4, f5, f6, f7, f8, f9, f10, f11) \
  ZJSON_TRANSFORM_FIELDS_10(StructType, f1, f2, f3, f4, f5, f6, f7, f8, f9, f10), ZJSON_FIELD_AUTO(StructType, f11)

#define ZJSON_TRANSFORM_FIELDS_12(StructType, f1, f2, f3, f4, f5, f6, f7, f8, f9, f10, f11, f12) \
  ZJSON_TRANSFORM_FIELDS_11(StructType, f1, f2, f3, f4, f5, f6, f7, f8, f9, f10, f11), ZJSON_FIELD_AUTO(StructType, f12)

#define ZJSON_TRANSFORM_FIELDS_13(StructType, f1, f2, f3, f4, f5, f6, f7, f8, f9, f10, f11, f12, f13) \
  ZJSON_TRANSFORM_FIELDS_12(StructType, f1, f2, f3, f4, f5, f6, f7, f8, f9, f10, f11, f12), ZJSON_FIELD_AUTO(StructType, f13)

#define ZJSON_TRANSFORM_FIELDS_14(StructType, f1, f2, f3, f4, f5, f6, f7, f8, f9, f10, f11, f12, f13, f14) \
  ZJSON_TRANSFORM_FIELDS_13(StructType, f1, f2, f3, f4, f5, f6, f7, f8, f9, f10, f11, f12, f13), ZJSON_FIELD_AUTO(StructType, f14)

#define ZJSON_TRANSFORM_FIELDS_15(StructType, f1, f2, f3, f4, f5, f6, f7, f8, f9, f10, f11, f12, f13, f14, f15) \
  ZJSON_TRANSFORM_FIELDS_14(StructType, f1, f2, f3, f4, f5, f6, f7, f8, f9, f10, f11, f12, f13, f14), ZJSON_FIELD_AUTO(StructType, f15)

#define ZJSON_TRANSFORM_FIELDS_16(StructType, f1, f2, f3, f4, f5, f6, f7, f8, f9, f10, f11, f12, f13, f14, f15, f16) \
  ZJSON_TRANSFORM_FIELDS_15(StructType, f1, f2, f3, f4, f5, f6, f7, f8, f9, f10, f11, f12, f13, f14, f15), ZJSON_FIELD_AUTO(StructType, f16)

#define ZJSON_TRANSFORM_FIELDS_17(StructType, f1, f2, f3, f4, f5, f6, f7, f8, f9, f10, f11, f12, f13, f14, f15, f16, f17) \
  ZJSON_TRANSFORM_FIELDS_16(StructType, f1, f2, f3, f4, f5, f6, f7, f8, f9, f10, f11, f12, f13, f14, f15, f16), ZJSON_FIELD_AUTO(StructType, f17)

#define ZJSON_TRANSFORM_FIELDS_18(StructType, f1, f2, f3, f4, f5, f6, f7, f8, f9, f10, f11, f12, f13, f14, f15, f16, f17, f18) \
  ZJSON_TRANSFORM_FIELDS_17(StructType, f1, f2, f3, f4, f5, f6, f7, f8, f9, f10, f11, f12, f13, f14, f15, f16, f17), ZJSON_FIELD_AUTO(StructType, f18)

#define ZJSON_TRANSFORM_FIELDS_19(StructType, f1, f2, f3, f4, f5, f6, f7, f8, f9, f10, f11, f12, f13, f14, f15, f16, f17, f18, f19) \
  ZJSON_TRANSFORM_FIELDS_18(StructType, f1, f2, f3, f4, f5, f6, f7, f8, f9, f10, f11, f12, f13, f14, f15, f16, f17, f18), ZJSON_FIELD_AUTO(StructType, f19)

#define ZJSON_TRANSFORM_FIELDS_20(StructType, f1, f2, f3, f4, f5, f6, f7, f8, f9, f10, f11, f12, f13, f14, f15, f16, f17, f18, f19, f20) \
  ZJSON_TRANSFORM_FIELDS_19(StructType, f1, f2, f3, f4, f5, f6, f7, f8, f9, f10, f11, f12, f13, f14, f15, f16, f17, f18, f19), ZJSON_FIELD_AUTO(StructType, f20)

#define ZJSON_TRANSFORM_FIELDS_21(StructType, f1, f2, f3, f4, f5, f6, f7, f8, f9, f10, f11, f12, f13, f14, f15, f16, f17, f18, f19, f20, f21) \
  ZJSON_TRANSFORM_FIELDS_20(StructType, f1, f2, f3, f4, f5, f6, f7, f8, f9, f10, f11, f12, f13, f14, f15, f16, f17, f18, f19, f20), ZJSON_FIELD_AUTO(StructType, f21)

#define ZJSON_TRANSFORM_FIELDS_22(StructType, f1, f2, f3, f4, f5, f6, f7, f8, f9, f10, f11, f12, f13, f14, f15, f16, f17, f18, f19, f20, f21, f22) \
  ZJSON_TRANSFORM_FIELDS_21(StructType, f1, f2, f3, f4, f5, f6, f7, f8, f9, f10, f11, f12, f13, f14, f15, f16, f17, f18, f19, f20, f21), ZJSON_FIELD_AUTO(StructType, f22)

#define ZJSON_TRANSFORM_FIELDS_23(StructType, f1, f2, f3, f4, f5, f6, f7, f8, f9, f10, f11, f12, f13, f14, f15, f16, f17, f18, f19, f20, f21, f22, f23) \
  ZJSON_TRANSFORM_FIELDS_22(StructType, f1, f2, f3, f4, f5, f6, f7, f8, f9, f10, f11, f12, f13, f14, f15, f16, f17, f18, f19, f20, f21, f22), ZJSON_FIELD_AUTO(StructType, f23)

#define ZJSON_TRANSFORM_FIELDS_24(StructType, f1, f2, f3, f4, f5, f6, f7, f8, f9, f10, f11, f12, f13, f14, f15, f16, f17, f18, f19, f20, f21, f22, f23, f24) \
  ZJSON_TRANSFORM_FIELDS_23(StructType, f1, f2, f3, f4, f5, f6, f7, f8, f9, f10, f11, f12, f13, f14, f15, f16, f17, f18, f19, f20, f21, f22, f23), ZJSON_FIELD_AUTO(StructType, f24)

#define ZJSON_TRANSFORM_FIELDS_25(StructType, f1, f2, f3, f4, f5, f6, f7, f8, f9, f10, f11, f12, f13, f14, f15, f16, f17, f18, f19, f20, f21, f22, f23, f24, f25) \
  ZJSON_TRANSFORM_FIELDS_24(StructType, f1, f2, f3, f4, f5, f6, f7, f8, f9, f10, f11, f12, f13, f14, f15, f16, f17, f18, f19, f20, f21, f22, f23, f24), ZJSON_FIELD_AUTO(StructType, f25)

#define ZJSON_TRANSFORM_FIELDS_26(StructType, f1, f2, f3, f4, f5, f6, f7, f8, f9, f10, f11, f12, f13, f14, f15, f16, f17, f18, f19, f20, f21, f22, f23, f24, f25, f26) \
  ZJSON_TRANSFORM_FIELDS_25(StructType, f1, f2, f3, f4, f5, f6, f7, f8, f9, f10, f11, f12, f13, f14, f15, f16, f17, f18, f19, f20, f21, f22, f23, f24, f25), ZJSON_FIELD_AUTO(StructType, f26)

#define ZJSON_TRANSFORM_FIELDS_27(StructType, f1, f2, f3, f4, f5, f6, f7, f8, f9, f10, f11, f12, f13, f14, f15, f16, f17, f18, f19, f20, f21, f22, f23, f24, f25, f26, f27) \
  ZJSON_TRANSFORM_FIELDS_26(StructType, f1, f2, f3, f4, f5, f6, f7, f8, f9, f10, f11, f12, f13, f14, f15, f16, f17, f18, f19, f20, f21, f22, f23, f24, f25, f26), ZJSON_FIELD_AUTO(StructType, f27)

#define ZJSON_TRANSFORM_FIELDS_28(StructType, f1, f2, f3, f4, f5, f6, f7, f8, f9, f10, f11, f12, f13, f14, f15, f16, f17, f18, f19, f20, f21, f22, f23, f24, f25, f26, f27, f28) \
  ZJSON_TRANSFORM_FIELDS_27(StructType, f1, f2, f3, f4, f5, f6, f7, f8, f9, f10, f11, f12, f13, f14, f15, f16, f17, f18, f19, f20, f21, f22, f23, f24, f25, f26, f27), ZJSON_FIELD_AUTO(StructType, f28)

#define ZJSON_TRANSFORM_FIELDS_29(StructType, f1, f2, f3, f4, f5, f6, f7, f8, f9, f10, f11, f12, f13, f14, f15, f16, f17, f18, f19, f20, f21, f22, f23, f24, f25, f26, f27, f28, f29) \
  ZJSON_TRANSFORM_FIELDS_28(StructType, f1, f2, f3, f4, f5, f6, f7, f8, f9, f10, f11, f12, f13, f14, f15, f16, f17, f18, f19, f20, f21, f22, f23, f24, f25, f26, f27, f28), ZJSON_FIELD_AUTO(StructType, f29)

#define ZJSON_TRANSFORM_FIELDS_30(StructType, f1, f2, f3, f4, f5, f6, f7, f8, f9, f10, f11, f12, f13, f14, f15, f16, f17, f18, f19, f20, f21, f22, f23, f24, f25, f26, f27, f28, f29, f30) \
  ZJSON_TRANSFORM_FIELDS_29(StructType, f1, f2, f3, f4, f5, f6, f7, f8, f9, f10, f11, f12, f13, f14, f15, f16, f17, f18, f19, f20, f21, f22, f23, f24, f25, f26, f27, f28, f29), ZJSON_FIELD_AUTO(StructType, f30)

#define ZJSON_TRANSFORM_FIELDS_31(StructType, f1, f2, f3, f4, f5, f6, f7, f8, f9, f10, f11, f12, f13, f14, f15, f16, f17, f18, f19, f20, f21, f22, f23, f24, f25, f26, f27, f28, f29, f30, f31) \
  ZJSON_TRANSFORM_FIELDS_30(StructType, f1, f2, f3, f4, f5, f6, f7, f8, f9, f10, f11, f12, f13, f14, f15, f16, f17, f18, f19, f20, f21, f22, f23, f24, f25, f26, f27, f28, f29, f30), ZJSON_FIELD_AUTO(StructType, f31)

#define ZJSON_TRANSFORM_FIELDS_32(StructType, f1, f2, f3, f4, f5, f6, f7, f8, f9, f10, f11, f12, f13, f14, f15, f16, f17, f18, f19, f20, f21, f22, f23, f24, f25, f26, f27, f28, f29, f30, f31, f32) \
  ZJSON_TRANSFORM_FIELDS_31(StructType, f1, f2, f3, f4, f5, f6, f7, f8, f9, f10, f11, f12, f13, f14, f15, f16, f17, f18, f19, f20, f21, f22, f23, f24, f25, f26, f27, f28, f29, f30, f31), ZJSON_FIELD_AUTO(StructType, f32)

// Argument counting macros for up to 32 arguments
#define ZJSON_GET_ARG_COUNT(...) ZJSON_GET_ARG_COUNT_IMPL(__VA_ARGS__,                                                        \
                                                          32, 31, 30, 29, 28, 27, 26, 25, 24, 23, 22, 21, 20, 19, 18, 17, 16, \
                                                          15, 14, 13, 12, 11, 10, 9, 8, 7, 6, 5, 4, 3, 2, 1)

#define ZJSON_GET_ARG_COUNT_IMPL(                                          \
    _1, _2, _3, _4, _5, _6, _7, _8, _9, _10, _11, _12, _13, _14, _15, _16, \
    _17, _18, _19, _20, _21, _22, _23, _24, _25, _26, _27, _28, _29, _30, _31, _32, N, ...) N

#define ZJSON_CONCAT(a, b) ZJSON_CONCAT_IMPL(a, b)
#define ZJSON_CONCAT_IMPL(a, b) a##b

// Main derive macro for automatic serialization
#define ZJSON_DERIVE(StructName, ...) \
  ZJSON_SERIALIZABLE(StructName, ZJSON_CONCAT(ZJSON_TRANSFORM_FIELDS_, ZJSON_GET_ARG_COUNT(__VA_ARGS__))(StructName, __VA_ARGS__))

// Core serializable macro
#define ZJSON_SERIALIZABLE(StructType, ...)                                                        \
  namespace zjson                                                                                  \
  {                                                                                                \
  template <>                                                                                      \
  struct Serializable<StructType>                                                                  \
  {                                                                                                \
    static constexpr bool value = true;                                                            \
    static Value serialize(const StructType &obj)                                                  \
    {                                                                                              \
      Value result = Value::create_object();                                                       \
      serialize_fields(obj, result, __VA_ARGS__);                                                  \
      return result;                                                                               \
    }                                                                                              \
  };                                                                                               \
  template <>                                                                                      \
  struct Deserializable<StructType>                                                                \
  {                                                                                                \
    static constexpr bool value = true;                                                            \
    static zstd::expected<StructType, Error> deserialize(const Value &value)                       \
    {                                                                                              \
      if (!value.is_object())                                                                      \
      {                                                                                            \
        return zstd::make_unexpected(Error::invalid_type("object", "other"));                      \
      }                                                                                            \
      StructType result{};                                                                         \
      const auto &obj = value.as_object();                                                         \
      bool deserialize_result = deserialize_fields(result, obj, __VA_ARGS__);                      \
      if (!deserialize_result)                                                                     \
      {                                                                                            \
        return zstd::make_unexpected(Error::invalid_data("Failed to deserialize fields"));         \
      }                                                                                            \
      if constexpr (zjson::detail::StructConfig<StructType>::deny_unknown_fields)                  \
      {                                                                                            \
        auto validation_result = detail::validate_no_unknown_fields<StructType>(obj, __VA_ARGS__); \
        if (!validation_result.has_value())                                                        \
        {                                                                                          \
          return zstd::make_unexpected(validation_result.error());                                 \
        }                                                                                          \
      }                                                                                            \
      return result;                                                                               \
    }                                                                                              \
  };                                                                                               \
  }

// Field serialization/deserialization helper functions
namespace detail
{

template <typename T, typename FieldType>
void serialize_field(const T &obj, Value &result, const Field<T, FieldType> &field)
{
  if (field.skip_serializing)
  {
    return;
  }

  if constexpr (Serializable<FieldType>::value)
  {
    const FieldType &field_value = obj.*(field.member);

    if (field.flatten_field)
    {
      Value nested_value = Serializable<FieldType>::serialize(field_value);
      if (nested_value.is_object())
      {
        for (const auto &[key, val] : nested_value.as_object())
        {
          if (result.is_object() && result.as_object().find(key) != result.as_object().end())
          {
            throw Error::invalid_data("Field name collision during flattening: '" + key + "' already exists in parent struct");
          }
          result.add_to_object(key, val);
        }
        return;
      }
    }

    if constexpr (is_optional<FieldType>::value)
    {
      if (!field_value.has_value() && field.skip_if_none)
      {
        return;
      }
    }

    result.add_to_object(field.get_serialized_name(), Serializable<FieldType>::serialize(field_value));
  }
  else
  {
    throw Error::invalid_data("Field '" + field.get_serialized_name() + "' has no serializer");
  }
}

template <typename T, typename FieldType>
bool deserialize_field(T &obj, const ObjectMap &object, const Field<T, FieldType> &field)
{
  if (field.skip_deserializing)
  {
    return true;
  }

  if constexpr (!Deserializable<FieldType>::value)
  {
    return false;
  }
  else
  {
    if (field.flatten_field)
    {
      Value flattened_value = Value::create_object();
      for (const auto &[key, val] : object)
      {
        flattened_value.add_to_object(key, val);
      }
      auto result = Deserializable<FieldType>::deserialize(flattened_value);
      if (result.has_value())
      {
        obj.*(field.member) = result.value();
        return true;
      }
      return false;
    }

    auto it = object.find(field.get_serialized_name());
    if (it == object.end())
    {
      if (field.default_value)
      {
        obj.*(field.member) = field.default_value();
        return true;
      }

      if constexpr (is_optional<FieldType>::value)
      {
        obj.*(field.member) = FieldType{};
        return true;
      }

      return false;
    }

    auto result = Deserializable<FieldType>::deserialize(it->second);
    if (!result.has_value())
    {
      return false;
    }
    obj.*(field.member) = result.value();
    return true;
  }
}

template <typename T, typename... Fields>
void serialize_fields(const T &obj, Value &result, const Fields &...fields)
{
  (serialize_field(obj, result, fields), ...);
}

template <typename T, typename... Fields>
bool deserialize_fields(T &obj, const ObjectMap &object, const Fields &...fields)
{
  return (deserialize_field(obj, object, fields) && ...);
}

template <typename... Fields>
bool has_flattened_field(const Fields &...fields)
{
  return (fields.flatten_field || ...);
}

template <typename T, typename... Fields>
zstd::expected<bool, Error> validate_no_unknown_fields(const ObjectMap &object, const Fields &...fields)
{
  // Check if any field is flattened - if so, we cannot reliably validate unknown fields
  if (has_flattened_field(fields...))
  {
    // When flattening is used, we cannot perform strict unknown field validation
    // because flattened fields effectively "consume" arbitrary field names
    return true;
  }

  // Check for unknown fields
  for (const auto &entry : object)
  {
    const auto &key = entry.first;
    if (!((key == fields.get_serialized_name()) || ...))
    {
      return zstd::make_unexpected(Error::unknown_field(key));
    }
  }

  return true;
}

} // namespace detail

// Expose helper functions
using detail::deserialize_fields;
using detail::serialize_fields;

/**
 * Container attributes for JSON serialization
 */

// Container attribute macros

#define ZJSON_RENAME_ALL(StructType, case_style)                     \
  namespace zjson                                                    \
  {                                                                  \
  namespace detail                                                   \
  {                                                                  \
  template <>                                                        \
  struct RenameAllConfig<StructType>                                 \
  {                                                                  \
    static constexpr auto value = attributes::RenameAll::case_style; \
  };                                                                 \
  }                                                                  \
  }

#define ZJSON_DENY_UNKNOWN_FIELDS(StructType) \
  namespace zjson                             \
  {                                           \
  namespace detail                            \
  {                                           \
  template <>                                 \
  struct DenyUnknownFieldsConfig<StructType>  \
  {                                           \
    static constexpr bool value = true;       \
  };                                          \
  }                                           \
  }

// Field attribute macros
#define zjson_rename(name) .rename(name)
#define zjson_skip() .skip()
#define zjson_skip_serializing() .skip_serializing_field()
#define zjson_skip_deserializing() .skip_deserializing_field()
#define zjson_skip_serializing_if_none() .skip_serializing_if_none()
#define zjson_default(func) .with_default(func)
#define zjson_flatten() .flatten()

} // namespace zjson

#endif // ZJSON_HPP
