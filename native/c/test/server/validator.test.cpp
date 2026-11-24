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

// ============================================================================
// Validator Test Suite - Testing validator.hpp API functionality
// ============================================================================

#include "../ztest.hpp"
#include "../../server/validator.hpp"
#include "../../zjson.hpp"
#include <string>

using namespace ztst;
using namespace validator;

// ============================================================================
// TEST HELPER FUNCTIONS
// ============================================================================

static zjson::Value create_test_object(const std::string &json_str)
{
  return zjson::parse_json_string(json_str);
}

// ============================================================================
// BASIC VALIDATION TESTS
// ============================================================================

void test_basic_validation()
{
  describe("Basic Validation Tests", []()
  {
    it("should detect missing required fields", []()
    {
      FieldDescriptor schema[] = {FieldDescriptor("name", FieldType::TYPE_STRING, true),
                                  FieldDescriptor("age", FieldType::TYPE_NUMBER, true)};

      auto params = create_test_object(R"({"name": "John"})");
      auto result = validate_schema(params, schema, 2, false);

      Expect(result.is_valid).ToBe(false);
      Expect(result.error_message.find("Missing required field: age") != std::string::npos).ToBe(true);
    });

    it("should pass when all required fields are present", []()
    {
      FieldDescriptor schema[] = {FieldDescriptor("name", FieldType::TYPE_STRING, true),
                                  FieldDescriptor("age", FieldType::TYPE_NUMBER, true)};

      auto params = create_test_object(R"({"name": "John", "age": 30})");
      auto result = validate_schema(params, schema, 2, false);

      Expect(result.is_valid).ToBe(true);
    });

    it("should allow missing optional fields", []()
    {
      FieldDescriptor schema[] = {FieldDescriptor("name", FieldType::TYPE_STRING, true),
                                  FieldDescriptor("email", FieldType::TYPE_STRING, false)};

      auto params = create_test_object(R"({"name": "John"})");
      auto result = validate_schema(params, schema, 2, false);

      Expect(result.is_valid).ToBe(true);
    });

    it("should allow null values for optional fields", []()
    {
      FieldDescriptor schema[] = {FieldDescriptor("name", FieldType::TYPE_STRING, true),
                                  FieldDescriptor("email", FieldType::TYPE_STRING, false)};

      auto params = create_test_object(R"({"name": "John", "email": null})");
      auto result = validate_schema(params, schema, 2, false);

      Expect(result.is_valid).ToBe(true);
    });
  });
}

// ============================================================================
// TYPE VALIDATION TESTS
// ============================================================================

void test_type_validation()
{
  describe("Type Validation Tests", []()
  {
    it("should validate string types correctly", []()
    {
      FieldDescriptor schema[] = {FieldDescriptor("name", FieldType::TYPE_STRING, true)};

      auto params = create_test_object(R"({"name": "John"})");
      auto result = validate_schema(params, schema, 1, false);

      Expect(result.is_valid).ToBe(true);
    });

    it("should reject wrong string types", []()
    {
      FieldDescriptor schema[] = {FieldDescriptor("name", FieldType::TYPE_STRING, true)};

      auto params = create_test_object(R"({"name": 123})");
      auto result = validate_schema(params, schema, 1, false);

      Expect(result.is_valid).ToBe(false);
      Expect(result.error_message.find("Expected string, got number") != std::string::npos).ToBe(true);
    });

    it("should validate integer numbers", []()
    {
      FieldDescriptor schema[] = {FieldDescriptor("age", FieldType::TYPE_NUMBER, true)};

      auto params = create_test_object(R"({"age": 30})");
      auto result = validate_schema(params, schema, 1, false);

      Expect(result.is_valid).ToBe(true);
    });

    it("should validate double numbers", []()
    {
      FieldDescriptor schema[] = {FieldDescriptor("price", FieldType::TYPE_NUMBER, true)};

      auto params = create_test_object(R"({"price": 29.99})");
      auto result = validate_schema(params, schema, 1, false);

      Expect(result.is_valid).ToBe(true);
    });

    it("should validate boolean types", []()
    {
      FieldDescriptor schema[] = {FieldDescriptor("active", FieldType::TYPE_BOOL, true)};

      auto params = create_test_object(R"({"active": true})");
      auto result = validate_schema(params, schema, 1, false);

      Expect(result.is_valid).ToBe(true);
    });

    it("should validate array types", []()
    {
      FieldDescriptor schema[] = {FieldDescriptor("items", FieldType::TYPE_ARRAY, true)};

      auto params = create_test_object(R"({"items": [1, 2, 3]})");
      auto result = validate_schema(params, schema, 1, false);

      Expect(result.is_valid).ToBe(true);
    });

    it("should validate object types", []()
    {
      FieldDescriptor schema[] = {FieldDescriptor("config", FieldType::TYPE_OBJECT, true)};

      auto params = create_test_object(R"({"config": {"key": "value"}})");
      auto result = validate_schema(params, schema, 1, false);

      Expect(result.is_valid).ToBe(true);
    });

    it("should accept any type for TYPE_ANY", []()
    {
      FieldDescriptor schema[] = {FieldDescriptor("data", FieldType::TYPE_ANY, true)};

      auto params1 = create_test_object(R"({"data": "string"})");
      auto result1 = validate_schema(params1, schema, 1, false);
      Expect(result1.is_valid).ToBe(true);

      auto params2 = create_test_object(R"({"data": 123})");
      auto result2 = validate_schema(params2, schema, 1, false);
      Expect(result2.is_valid).ToBe(true);

      auto params3 = create_test_object(R"({"data": {"nested": true}})");
      auto result3 = validate_schema(params3, schema, 1, false);
      Expect(result3.is_valid).ToBe(true);
    });
  });
}

// ============================================================================
// UNKNOWN FIELD DETECTION TESTS
// ============================================================================

void test_unknown_fields()
{
  describe("Unknown Field Detection Tests", []()
  {
    it("should reject unknown fields when disallowed", []()
    {
      FieldDescriptor schema[] = {FieldDescriptor("name", FieldType::TYPE_STRING, true)};

      auto params = create_test_object(R"({"name": "John", "unknown": "value"})");
      auto result = validate_schema(params, schema, 1, false);

      Expect(result.is_valid).ToBe(false);
      Expect(result.error_message.find("Unknown field: unknown") != std::string::npos).ToBe(true);
    });

    it("should allow unknown fields when allowed", []()
    {
      FieldDescriptor schema[] = {FieldDescriptor("name", FieldType::TYPE_STRING, true)};

      auto params = create_test_object(R"({"name": "John", "unknown": "value"})");
      auto result = validate_schema(params, schema, 1, true);

      Expect(result.is_valid).ToBe(true);
    });
  });
}

// ============================================================================
// ARRAY ELEMENT TYPE VALIDATION TESTS
// ============================================================================

void test_array_validation()
{
  describe("Array Element Type Validation Tests", []()
  {
    it("should validate array element types correctly", []()
    {
      FieldDescriptor schema[] = {FieldDescriptor("numbers", FieldType::TYPE_ARRAY, true, FieldType::TYPE_NUMBER)};

      auto params = create_test_object(R"({"numbers": [1, 2, 3]})");
      auto result = validate_schema(params, schema, 1, false);

      Expect(result.is_valid).ToBe(true);
    });

    it("should reject wrong array element types", []()
    {
      FieldDescriptor schema[] = {FieldDescriptor("numbers", FieldType::TYPE_ARRAY, true, FieldType::TYPE_NUMBER)};

      auto params = create_test_object(R"({"numbers": ["not", "numbers"]})");
      auto result = validate_schema(params, schema, 1, false);

      Expect(result.is_valid).ToBe(false);
      Expect(result.error_message.find("numbers[0]") != std::string::npos).ToBe(true);
      Expect(result.error_message.find("Expected number, got string") != std::string::npos).ToBe(true);
    });

    it("should allow empty arrays", []()
    {
      FieldDescriptor schema[] = {FieldDescriptor("numbers", FieldType::TYPE_ARRAY, true, FieldType::TYPE_NUMBER)};

      auto params = create_test_object(R"({"numbers": []})");
      auto result = validate_schema(params, schema, 1, false);

      Expect(result.is_valid).ToBe(true);
    });
  });
}

// ============================================================================
// NESTED OBJECT VALIDATION TESTS
// ============================================================================

void test_nested_validation()
{
  describe("Nested Object Validation Tests", []()
  {
    it("should validate nested objects correctly", []()
    {
      FieldDescriptor nested_schema[] = {FieldDescriptor("host", FieldType::TYPE_STRING, true),
                                         FieldDescriptor("port", FieldType::TYPE_NUMBER, true)};

      FieldDescriptor schema[] = {FieldDescriptor("config", FieldType::TYPE_OBJECT, true)};
      schema[0].nested_schema = nested_schema;
      schema[0].nested_schema_count = 2;

      auto params = create_test_object(R"({"config": {"host": "localhost", "port": 8080}})");
      auto result = validate_schema(params, schema, 1, false);

      Expect(result.is_valid).ToBe(true);
    });

    it("should detect missing required fields in nested objects", []()
    {
      FieldDescriptor nested_schema[] = {FieldDescriptor("host", FieldType::TYPE_STRING, true),
                                         FieldDescriptor("port", FieldType::TYPE_NUMBER, true)};

      FieldDescriptor schema[] = {FieldDescriptor("config", FieldType::TYPE_OBJECT, true)};
      schema[0].nested_schema = nested_schema;
      schema[0].nested_schema_count = 2;

      auto params = create_test_object(R"({"config": {"host": "localhost"}})");
      auto result = validate_schema(params, schema, 1, false);

      Expect(result.is_valid).ToBe(false);
      Expect(result.error_message.find("Missing required field: config.port") != std::string::npos).ToBe(true);
    });

    it("should detect wrong types in nested objects", []()
    {
      FieldDescriptor nested_schema[] = {FieldDescriptor("host", FieldType::TYPE_STRING, true),
                                         FieldDescriptor("port", FieldType::TYPE_NUMBER, true)};

      FieldDescriptor schema[] = {FieldDescriptor("config", FieldType::TYPE_OBJECT, true)};
      schema[0].nested_schema = nested_schema;
      schema[0].nested_schema_count = 2;

      auto params = create_test_object(R"({"config": {"host": "localhost", "port": "8080"}})");
      auto result = validate_schema(params, schema, 1, false);

      Expect(result.is_valid).ToBe(false);
      Expect(result.error_message.find("config.port") != std::string::npos).ToBe(true);
      Expect(result.error_message.find("Expected number, got string") != std::string::npos).ToBe(true);
    });

    it("should validate nested array objects", []()
    {
      FieldDescriptor nested_schema[] = {FieldDescriptor("id", FieldType::TYPE_NUMBER, true),
                                         FieldDescriptor("name", FieldType::TYPE_STRING, true)};

      FieldDescriptor schema[] = {FieldDescriptor("users", FieldType::TYPE_ARRAY, true, FieldType::TYPE_OBJECT)};
      schema[0].nested_schema = nested_schema;
      schema[0].nested_schema_count = 2;

      auto params = create_test_object(R"({"users": [{"id": 1, "name": "John"}]})");
      auto result = validate_schema(params, schema, 1, false);

      Expect(result.is_valid).ToBe(true);
    });

    it("should detect errors in nested array objects", []()
    {
      FieldDescriptor nested_schema[] = {FieldDescriptor("id", FieldType::TYPE_NUMBER, true),
                                         FieldDescriptor("name", FieldType::TYPE_STRING, true)};

      FieldDescriptor schema[] = {FieldDescriptor("users", FieldType::TYPE_ARRAY, true, FieldType::TYPE_OBJECT)};
      schema[0].nested_schema = nested_schema;
      schema[0].nested_schema_count = 2;

      auto params = create_test_object(R"({"users": [{"id": "not_number", "name": "John"}]})");
      auto result = validate_schema(params, schema, 1, false);

      Expect(result.is_valid).ToBe(false);
      Expect(result.error_message.find("users[0].id") != std::string::npos).ToBe(true);
      Expect(result.error_message.find("Expected number, got string") != std::string::npos).ToBe(true);
    });
  });
}

// ============================================================================
// ERROR CASES TESTS
// ============================================================================

void test_error_cases()
{
  describe("Error Cases Tests", []()
  {
    it("should reject non-object parameters", []()
    {
      FieldDescriptor schema[] = {FieldDescriptor("name", FieldType::TYPE_STRING, true)};

      zjson::Value params("not an object");
      auto result = validate_schema(params, schema, 1, false);

      Expect(result.is_valid).ToBe(false);
      Expect(result.error_message.find("Parameters must be an object") != std::string::npos).ToBe(true);
    });

    it("should handle empty schema correctly", []()
    {
      auto params = create_test_object(R"({})");
      auto result = validate_schema(params, nullptr, 0, false);

      Expect(result.is_valid).ToBe(true);
    });

    it("should detect unknown fields with empty schema", []()
    {
      auto params = create_test_object(R"({"unknown": "value"})");
      auto result = validate_schema(params, nullptr, 0, false);

      Expect(result.is_valid).ToBe(false);
      Expect(result.error_message.find("Unknown field: unknown") != std::string::npos).ToBe(true);
    });

    it("should enforce depth limits", []()
    {
      FieldDescriptor nested_schema[] = {FieldDescriptor("deep", FieldType::TYPE_OBJECT, true)};
      nested_schema[0].nested_schema = nested_schema;
      nested_schema[0].nested_schema_count = 1;

      FieldDescriptor schema[] = {FieldDescriptor("config", FieldType::TYPE_OBJECT, true)};
      schema[0].nested_schema = nested_schema;
      schema[0].nested_schema_count = 1;

      auto params = create_test_object(R"({"config": {"deep": {"deeper": "value"}}})");
      auto result = validate_schema(params, schema, 1, false, "parent");

      Expect(result.is_valid).ToBe(false);
      Expect(result.error_message.find("Nested schemas beyond 1 level deep are not supported") != std::string::npos)
          .ToBe(true);
    });
  });
}

// ============================================================================
// TEST RUNNER
// ============================================================================

void server_validator_tests()
{
  test_basic_validation();
  test_type_validation();
  test_unknown_fields();
  test_array_validation();
  test_nested_validation();
  test_error_cases();
}