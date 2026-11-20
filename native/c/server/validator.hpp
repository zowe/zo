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

#ifndef VALIDATOR_HPP
#define VALIDATOR_HPP

#include <cstddef>
#include <string>
#include <string_view>

/**
 * Schema-based validation for JSON-RPC messages.
 *
 * Define schemas using ZJSON_SCHEMA macro, then register with CommandBuilder:
 *
 *   struct ListDatasetRequest {};
 *   ZJSON_SCHEMA(ListDatasetRequest,
 *       FIELD_REQUIRED(pattern, STRING),
 *       FIELD_OPTIONAL(attributes, BOOL)
 *   )
 *
 *   CommandBuilder(handler).validate<ListDatasetRequest, ListDatasetResponse>()
 *
 * Available field types: BOOL, NUMBER, STRING, ARRAY, OBJECT, ANY
 */

// Forward declaration for types that depend on zjson
namespace zjson
{
class Value;
}

namespace validator
{

/**
 * Field types for schema validation
 */
enum class FieldType
{
  TYPE_BOOL,
  TYPE_NUMBER,
  TYPE_STRING,
  TYPE_ARRAY,
  TYPE_OBJECT,
  TYPE_ANY
};

/**
 * Schema field descriptor
 */
struct FieldDescriptor
{
  std::string_view name;
  FieldType type;
  bool required;
  FieldType array_element_type;
  const FieldDescriptor *nested_schema = nullptr;
  size_t nested_schema_count = 0;

  constexpr FieldDescriptor(std::string_view n, FieldType t, bool req)
      : name(n), type(t), required(req), array_element_type(FieldType::TYPE_ANY)
  {
  }

  constexpr FieldDescriptor(std::string_view n, FieldType t, bool req, FieldType array_elem_type)
      : name(n), type(t), required(req), array_element_type(array_elem_type)
  {
  }

  constexpr FieldDescriptor(std::string_view n, FieldType t, bool req, const FieldDescriptor *nested, size_t nested_count)
      : name(n), type(t), required(req), array_element_type(FieldType::TYPE_ANY), nested_schema(nested), nested_schema_count(nested_count)
  {
  }

  constexpr FieldDescriptor(std::string_view n, FieldType t, bool req, FieldType array_elem_type, const FieldDescriptor *nested, size_t nested_count)
      : name(n), type(t), required(req), array_element_type(array_elem_type), nested_schema(nested), nested_schema_count(nested_count)
  {
  }
};

/**
 * Schema registry - maps struct types to their schema definitions
 */
template <typename T>
struct SchemaRegistry
{
  inline static constexpr const FieldDescriptor *fields = nullptr;
  inline static constexpr size_t field_count = 0;
};

/**
 * Validation result type
 */
struct ValidationResult
{
  bool is_valid;
  std::string error_message;

  static ValidationResult success()
  {
    return ValidationResult{true, ""};
  }

  static ValidationResult error(const std::string &message)
  {
    return ValidationResult{false, message};
  }
};

struct SchemaView
{
  const FieldDescriptor *fields;
  size_t field_count;
};

/**
 * Validate a JSON value against a schema
 */
ValidationResult validate_schema(const zjson::Value &params,
                                 const FieldDescriptor *schema,
                                 size_t field_count,
                                 bool allow_unknown_fields = false,
                                 const std::string &parent_field = "");

} // namespace validator

// Macros for defining schemas

#define FIELD_REQUIRED(name, type) \
  validator::FieldDescriptor(#name, validator::FieldType::TYPE_##type, true)

#define FIELD_OPTIONAL(name, type) \
  validator::FieldDescriptor(#name, validator::FieldType::TYPE_##type, false)

#define FIELD_REQUIRED_ARRAY(name, element_type) \
  validator::FieldDescriptor(#name, validator::FieldType::TYPE_ARRAY, true, validator::FieldType::TYPE_##element_type)

#define FIELD_OPTIONAL_ARRAY(name, element_type) \
  validator::FieldDescriptor(#name, validator::FieldType::TYPE_ARRAY, false, validator::FieldType::TYPE_##element_type)

#define FIELD_REQUIRED_OBJECT(name, StructType) \
  validator::FieldDescriptor(#name, validator::FieldType::TYPE_OBJECT, true, validator::SchemaRegistry<StructType>::fields, validator::SchemaRegistry<StructType>::field_count)

#define FIELD_OPTIONAL_OBJECT(name, StructType) \
  validator::FieldDescriptor(#name, validator::FieldType::TYPE_OBJECT, false, validator::SchemaRegistry<StructType>::fields, validator::SchemaRegistry<StructType>::field_count)

#define FIELD_REQUIRED_OBJECT_ARRAY(name, StructType) \
  validator::FieldDescriptor(#name, validator::FieldType::TYPE_ARRAY, true, validator::FieldType::TYPE_OBJECT, validator::SchemaRegistry<StructType>::fields, validator::SchemaRegistry<StructType>::field_count)

#define FIELD_OPTIONAL_OBJECT_ARRAY(name, StructType) \
  validator::FieldDescriptor(#name, validator::FieldType::TYPE_ARRAY, false, validator::FieldType::TYPE_OBJECT, validator::SchemaRegistry<StructType>::fields, validator::SchemaRegistry<StructType>::field_count)

#define ZJSON_SCHEMA(StructType, ...)                              \
  namespace validator                                              \
  {                                                                \
  inline constexpr FieldDescriptor StructType##_schema_array[] = { \
      __VA_ARGS__};                                                \
  template <>                                                      \
  inline constexpr const FieldDescriptor *SchemaRegistry<StructType>::fields = \
      StructType##_schema_array;                                   \
  template <>                                                      \
  inline constexpr size_t SchemaRegistry<StructType>::field_count = \
      sizeof(StructType##_schema_array) / sizeof(FieldDescriptor); \
  }

#endif // VALIDATOR_HPP
