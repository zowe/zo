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
// ZJson Test Suite - Testing zjson.hpp API functionality
// ============================================================================

#include "ztest.hpp"
#include <string>
#include <vector>
#include <cassert>
#include "../zjson.hpp"

using namespace ztst;

// ============================================================================
// TEST STRUCTURES - Focused on API testing (examples covered in json.cpp)
// ============================================================================

struct SimpleStruct
{
  int id;
  std::string name;
};

struct UnregisteredType
{
  int value;
};

struct SkipUnregisteredField
{
  int id;
  UnregisteredType ignored;
};

struct SkipSerializationOnly
{
  int id;
  UnregisteredType ignored;
};

struct UnskippedUnregisteredField
{
  int id;
  UnregisteredType ignored;
};

struct TestOptional
{
  std::optional<int> opt_int;
  std::optional<std::string> opt_string;
};

// Test structs for flattening feature
struct BaseConfig
{
  std::string host;
  int port;
};

struct DatabaseConfig
{
  BaseConfig connection; // Will be flattened
  std::string database_name;
  bool ssl_enabled;
};

// Register minimal test types for API testing
ZJSON_DERIVE(SimpleStruct, id, name);
ZJSON_SERIALIZABLE(SkipUnregisteredField, ZJSON_FIELD(SkipUnregisteredField, id),
                   ZJSON_FIELD(SkipUnregisteredField, ignored).skip());
ZJSON_SERIALIZABLE(SkipSerializationOnly, ZJSON_FIELD(SkipSerializationOnly, id),
                   ZJSON_FIELD(SkipSerializationOnly, ignored).skip_serializing_field());
ZJSON_DERIVE(UnskippedUnregisteredField, id, ignored);
ZJSON_DERIVE(TestOptional, opt_int, opt_string);
ZJSON_DERIVE(BaseConfig, host, port);
ZJSON_SERIALIZABLE(DatabaseConfig, ZJSON_FIELD(DatabaseConfig, connection).flatten(),
                   ZJSON_FIELD(DatabaseConfig, database_name), ZJSON_FIELD(DatabaseConfig, ssl_enabled));

// ============================================================================
// TYPE TRAIT TESTS
// ============================================================================

void test_type_traits()
{
  describe("ZJson Type Trait Tests", []()
  {
    it("should correctly identify basic types as serializable", []()
    {
      bool bool_serializable = zjson::Serializable<bool>::value;
      bool int_serializable = zjson::Serializable<int>::value;
      bool longlong_serializable = zjson::Serializable<long long>::value;
      bool ulonglong_serializable = zjson::Serializable<unsigned long long>::value;
      bool string_serializable = zjson::Serializable<std::string>::value;
      Expect(bool_serializable).ToBe(true);
      Expect(int_serializable).ToBe(true);
      Expect(longlong_serializable).ToBe(true);
      Expect(ulonglong_serializable).ToBe(true);
      Expect(string_serializable).ToBe(true);
    });

    it("should correctly identify basic types as deserializable", []()
    {
      bool bool_deserializable = zjson::Deserializable<bool>::value;
      bool int_deserializable = zjson::Deserializable<int>::value;
      bool longlong_deserializable = zjson::Deserializable<long long>::value;
      bool ulonglong_deserializable = zjson::Deserializable<unsigned long long>::value;
      bool string_deserializable = zjson::Deserializable<std::string>::value;
      Expect(bool_deserializable).ToBe(true);
      Expect(int_deserializable).ToBe(true);
      Expect(longlong_deserializable).ToBe(true);
      Expect(ulonglong_deserializable).ToBe(true);
      Expect(string_deserializable).ToBe(true);
    });

    it("should correctly identify registered struct traits", []()
    {
      bool simple_serializable = zjson::Serializable<SimpleStruct>::value;
      bool simple_deserializable = zjson::Deserializable<SimpleStruct>::value;
      Expect(simple_serializable).ToBe(true);
      Expect(simple_deserializable).ToBe(true);
    });

    it("should correctly identify unregistered types as not serializable", []()
    {
      bool unreg_serializable = zjson::Serializable<UnregisteredType>::value;
      bool unreg_deserializable = zjson::Deserializable<UnregisteredType>::value;
      Expect(unreg_serializable).ToBe(false);
      Expect(unreg_deserializable).ToBe(false);
    });

    it("should correctly handle std::vector traits", []()
    {
      bool vec_int_serializable = zjson::Serializable<std::vector<int>>::value;
      bool vec_simple_serializable = zjson::Serializable<std::vector<SimpleStruct>>::value;
      bool vec_unreg_serializable = zjson::Serializable<std::vector<UnregisteredType>>::value;
      Expect(vec_int_serializable).ToBe(true);
      Expect(vec_simple_serializable).ToBe(true);
      Expect(vec_unreg_serializable).ToBe(false);
    });

    it("should correctly handle optional traits", []()
    {
      bool opt_int_serializable = zjson::Serializable<std::optional<int>>::value;
      bool opt_simple_serializable = zjson::Serializable<std::optional<SimpleStruct>>::value;
      bool opt_unreg_serializable = zjson::Serializable<std::optional<UnregisteredType>>::value;
      Expect(opt_int_serializable).ToBe(true);
      Expect(opt_simple_serializable).ToBe(true);
      Expect(opt_unreg_serializable).ToBe(false);
    });

    it("should support constexpr trait evaluation", []()
    {
      constexpr bool int_serializable = zjson::Serializable<int>::value;
      constexpr bool longlong_serializable = zjson::Serializable<long long>::value;
      constexpr bool ulonglong_serializable = zjson::Serializable<unsigned long long>::value;
      constexpr bool unreg_serializable = zjson::Serializable<UnregisteredType>::value;

      // Convert constexpr values to runtime values for testing
      bool int_ser = int_serializable;
      bool longlong_ser = longlong_serializable;
      bool ulonglong_ser = ulonglong_serializable;
      bool unreg_ser = unreg_serializable;
      Expect(int_ser).ToBe(true);
      Expect(longlong_ser).ToBe(true);
      Expect(ulonglong_ser).ToBe(true);
      Expect(unreg_ser).ToBe(false);
    });
  });
}

// ============================================================================
// VALUE TYPE TESTS
// ============================================================================

void test_value_types()
{
  describe("ZJson Value Type Tests", []()
  {
    it("should detect null values correctly", []()
    {
      zjson::Value null_val;
      Expect(null_val.is_null()).ToBe(true);
      Expect(null_val.get_type() == zjson::Value::Null).ToBe(true);
    });

    it("should detect boolean values correctly", []()
    {
      zjson::Value bool_val(true);
      Expect(bool_val.is_bool()).ToBe(true);
      Expect(bool_val.get_type() == zjson::Value::Bool).ToBe(true);
    });

    it("should detect numeric values correctly", []()
    {
      zjson::Value int_val(42);
      Expect(int_val.is_integer()).ToBe(true);
      Expect(int_val.get_type() == zjson::Value::Number).ToBe(true);

      zjson::Value double_val(3.14);
      Expect(double_val.is_double()).ToBe(true);
      Expect(double_val.get_type() == zjson::Value::Number).ToBe(true);
    });

    it("should detect string values correctly", []()
    {
      zjson::Value string_val("hello");
      Expect(string_val.is_string()).ToBe(true);
      Expect(string_val.get_type() == zjson::Value::String).ToBe(true);
    });

    it("should have exclusive type detection", []()
    {
      zjson::Value bool_val(true);
      Expect(bool_val.is_null()).ToBe(false);
      Expect(bool_val.is_string()).ToBe(false);

      zjson::Value string_val("hello");
      Expect(string_val.is_integer()).ToBe(false);
      Expect(string_val.is_double()).ToBe(false);
    });

    it("should support object indexing with string keys", []()
    {
      zjson::Value obj = zjson::Value::create_object();
      obj.add_to_object("name", zjson::Value("Alice"));
      obj.add_to_object("age", zjson::Value(30));

      // Test const access
      const zjson::Value &const_obj = obj;
      std::string name = const_obj["name"].as_string();
      int64_t age = const_obj["age"].as_int64();

      Expect(name == "Alice").ToBe(true);
      Expect(age).ToBe(30);

      // Test that missing keys return null
      const zjson::Value &missing = const_obj["nonexistent"];
      Expect(missing.is_null()).ToBe(true);
    });

    it("should support mutable object indexing", []()
    {
      zjson::Value obj = zjson::Value::create_object();

      // Test mutable access (creates entries)
      obj["name"] = zjson::Value("Bob");
      obj["age"] = zjson::Value(25);

      Expect(obj["name"].as_string() == "Bob").ToBe(true);
      Expect(obj["age"].as_int64()).ToBe(25);
    });

    it("should support array indexing with size_t indices", []()
    {
      zjson::Value arr = zjson::Value::create_array();
      arr.add_to_array(zjson::Value("first"));
      arr.add_to_array(zjson::Value("second"));
      arr.add_to_array(zjson::Value("third"));

      // Test const access
      const zjson::Value &const_arr = arr;
      std::string first = const_arr[0].as_string();
      std::string second = const_arr[1].as_string();

      Expect(first == "first").ToBe(true);
      Expect(second == "second").ToBe(true);

      // Test that out-of-bounds returns null
      const zjson::Value &missing = const_arr[10];
      Expect(missing.is_null()).ToBe(true);
    });

    it("should support mutable array indexing with auto-expansion", []()
    {
      zjson::Value arr = zjson::Value::create_array();

      // Test auto-expansion
      arr[0] = zjson::Value("first");
      arr[2] = zjson::Value("third"); // Creates gap at index 1

      Expect(arr[0].as_string() == "first").ToBe(true);
      Expect(arr[1].is_null()).ToBe(true); // Gap filled with null
      Expect(arr[2].as_string() == "third").ToBe(true);
    });

    it("should auto-convert null to object on string indexing", []()
    {
      zjson::Value null_val; // Starts as null
      Expect(null_val.is_null()).ToBe(true);

      // Should auto-convert to object
      null_val["key"] = zjson::Value("value");

      Expect(null_val.is_object()).ToBe(true);
      Expect(null_val["key"].as_string() == "value").ToBe(true);
    });

    it("should auto-convert null to array on numeric indexing", []()
    {
      zjson::Value null_val; // Starts as null
      Expect(null_val.is_null()).ToBe(true);

      // Should auto-convert to array
      null_val[0] = zjson::Value("item");

      Expect(null_val.is_array()).ToBe(true);
      Expect(null_val[0].as_string() == "item").ToBe(true);
    });
  });
}

// ============================================================================
// OPTIONAL FIELD TESTS
// ============================================================================

struct OptionalBehaviorStruct
{
  std::string name;
  std::optional<std::string> include_null; // Default: include null when empty
  std::optional<int> skip_if_none;         // Skip when empty
};

ZJSON_SERIALIZABLE(OptionalBehaviorStruct, ZJSON_FIELD(OptionalBehaviorStruct, name),
                   ZJSON_FIELD(OptionalBehaviorStruct, include_null),
                   ZJSON_FIELD(OptionalBehaviorStruct, skip_if_none) zjson_skip_serializing_if_none());

void test_optional_types()
{
  describe("ZJson Optional Field Tests", []()
  {
    it("should handle empty optionals", []()
    {
      std::optional<int> empty_opt;
      Expect(empty_opt.has_value()).ToBe(false);
    });

    it("should handle optionals with values", []()
    {
      std::optional<int> valued_opt(42);
      Expect(valued_opt.has_value()).ToBe(true);
      Expect(valued_opt.value()).ToBe(42);
    });

    it("should handle std::string optionals", []()
    {
      std::optional<std::string> string_opt("hello");
      Expect(string_opt.has_value()).ToBe(true);
      Expect(string_opt.value()).ToBe(std::string("hello"));
    });

    it("should support copying", []()
    {
      std::optional<int> valued_opt(42);
      std::optional<int> copied_opt = valued_opt;
      Expect(copied_opt.has_value()).ToBe(true);
      Expect(copied_opt.value()).ToBe(42);
    });

    it("should include null for empty optionals by default", []()
    {
      OptionalBehaviorStruct obj{
          "test",
          std::optional<std::string>(), // empty - should include as null
          std::optional<int>()          // empty - should skip due to attribute
      };

      auto json_result = zjson::to_string(obj);
      Expect(json_result.has_value()).ToBe(true);

      std::string json_str = json_result.value();

      // Should include include_null as null
      bool has_include_null = json_str.find("include_null") != std::string::npos;
      Expect(has_include_null).ToBe(true);

      // Should NOT include skip_if_none field
      bool has_skip_if_none = json_str.find("skip_if_none") != std::string::npos;
      Expect(has_skip_if_none).ToBe(false);
    });

    it("should serialize present optional values normally", []()
    {
      OptionalBehaviorStruct obj{"test", std::optional<std::string>("present_value"), std::optional<int>(42)};

      auto json_result = zjson::to_string(obj);
      Expect(json_result.has_value()).ToBe(true);

      auto restored_result = zjson::from_str<OptionalBehaviorStruct>(json_result.value());
      Expect(restored_result.has_value()).ToBe(true);

      OptionalBehaviorStruct restored = restored_result.value();
      Expect(restored.include_null.has_value()).ToBe(true);
      Expect(restored.include_null.value()).ToBe(std::string("present_value"));
      Expect(restored.skip_if_none.has_value()).ToBe(true);
      Expect(restored.skip_if_none.value()).ToBe(42);
    });
  });
}

// ============================================================================
// EXPECTED TYPE TESTS
// ============================================================================

void test_expected_types()
{
  describe("ZJson Expected Type Tests", []()
  {
    it("should handle successful results", []()
    {
      zstd::expected<int, zjson::Error> success_result(42);
      Expect(success_result.has_value()).ToBe(true);
      Expect(success_result.value()).ToBe(42);
    });

    it("should handle error results", []()
    {
      auto error = zjson::Error::invalid_value("test error");
      zstd::expected<int, zjson::Error> error_result = zstd::make_unexpected(error);
      Expect(error_result.has_value()).ToBe(false);
      Expect(error_result.error().kind() == zjson::Error::InvalidValue).ToBe(true);
    });

    it("should handle std::string results", []()
    {
      zstd::expected<std::string, zjson::Error> string_result("hello world");
      Expect(string_result.has_value()).ToBe(true);
      Expect(string_result.value()).ToBe(std::string("hello world"));
    });
  });
}

// ============================================================================
// BASIC API TESTS - Focused tests complementing json.cpp examples
// ============================================================================

void test_basic_api()
{
  describe("ZJson Basic API Tests", []()
  {
    it("should handle basic serialization API", []()
    {
      // Test that the API compiles and basic functionality works
      // Actual serialization examples are covered in json.cpp
      SimpleStruct simple{42, "test"};

      // Test that the serialization traits are properly set up
      bool is_serializable = zjson::Serializable<SimpleStruct>::value;
      Expect(is_serializable).ToBe(true);

      // Test basic Value operations
      zjson::Value test_val(42);
      Expect(test_val.is_integer()).ToBe(true);
    });

    it("should create valid field objects with ZJSON_FIELD macro", []()
    {
      auto test_field = ZJSON_FIELD(SimpleStruct, id);
      Expect(test_field.member != nullptr).ToBe(true);
    });

    it("should handle error cases in parsing", []()
    {
      std::string invalid_json = R"({"invalid": json})";
      auto result = zjson::from_str<zjson::Value>(invalid_json);
      Expect(result.has_value()).ToBe(false);

      const auto &error = result.error();
      std::string error_msg = error.what();
      Expect(error_msg.length() > 0).ToBe(true);
    });

    it("should reject a control character escape when it is the entire parsed value", []()
    {
      // A bare JSON string value containing a disallowed control character
      // escape must fail the whole parse rather than silently producing a
      // null/partial value.
      std::string json = "\"bad\\u0001value\"";
      auto result = zjson::from_str<zjson::Value>(json);
      Expect(result.has_value()).ToBe(false);
    });

    it("should reject a control character escape nested inside an object field", []()
    {
      // Previously, a bad nested string field was silently dropped from the
      // object and the rest of the request still parsed. It must now fail
      // the whole parse instead.
      std::string json = "{\"dataset\": \"MY.DATA\\u0001SET\", \"member\": \"OK\"}";
      auto result = zjson::from_str<zjson::Value>(json);
      Expect(result.has_value()).ToBe(false);
    });

    it("should reject a control character escape nested inside an array element", []()
    {
      std::string json = "[\"fine\", \"also fine\", \"bad\\u000Bvalue\"]";
      auto result = zjson::from_str<zjson::Value>(json);
      Expect(result.has_value()).ToBe(false);
    });

    it("should fail the whole parse when JSON nesting exceeds the depth limit", []()
    {
      // Build JSON nested well past MAX_JSON_DEPTH (64). Previously this
      // silently truncated to a null value at the depth limit; it must now
      // fail the whole parse.
      const int depth = 66;
      std::string json;
      for (int i = 0; i < depth; i++)
      {
        json += "{\"a\":";
      }
      json += "1";
      for (int i = 0; i < depth; i++)
      {
        json += "}";
      }

      auto result = zjson::from_str<zjson::Value>(json);
      Expect(result.has_value()).ToBe(false);
    });

    it("should support chained access patterns", []()
    {
      // Test parsing and chained access together
      std::string json = R"({
                "users": [
                    {"name": "Alice", "profile": {"age": 30}},
                    {"name": "Bob", "profile": {"age": 25}}
                ]
            })";

      auto result = zjson::from_str<zjson::Value>(json);
      Expect(result.has_value()).ToBe(true);

      zjson::Value root = result.value();

      // Test deep chained access
      std::string alice_name = root["users"][0]["name"].as_string();
      int64_t alice_age = root["users"][0]["profile"]["age"].as_int64();

      Expect(alice_name == "Alice").ToBe(true);
      Expect(alice_age).ToBe(30);

      // Test second element
      std::string bob_name = root["users"][1]["name"].as_string();
      int64_t bob_age = root["users"][1]["profile"]["age"].as_int64();

      Expect(bob_name == "Bob").ToBe(true);
      Expect(bob_age).ToBe(25);
    });

    it("should support dynamic construction with chaining", []()
    {
      zjson::Value root; // Starts as null

      // Build nested structure using chained assignment
      root["company"]["name"] = zjson::Value("Tech Corp");
      root["company"]["employees"][0]["name"] = zjson::Value("Alice");
      root["company"]["employees"][0]["id"] = zjson::Value(1);
      root["company"]["employees"][1]["name"] = zjson::Value("Bob");
      root["company"]["employees"][1]["id"] = zjson::Value(2);
      root["metadata"]["count"] = zjson::Value(2);

      // Verify the structure was created correctly
      Expect(root.is_object()).ToBe(true);
      Expect(root["company"]["name"].as_string() == "Tech Corp").ToBe(true);
      Expect(root["company"]["employees"][0]["name"].as_string() == "Alice").ToBe(true);
      Expect(root["company"]["employees"][1]["id"].as_int64()).ToBe(2);
      Expect(root["metadata"]["count"].as_int64()).ToBe(2);
    });
  });
}

// ============================================================================
// DYNAMIC ACCESS EDGE CASES
// ============================================================================

void test_dynamic_access_edge_cases()
{
  describe("ZJson Dynamic Access Edge Cases", []()
  {
    it("should handle type mismatch errors gracefully", []()
    {
      zjson::Value string_val("hello");

      try
      {
        // Should throw when trying to index string as object
        auto result = string_val["key"];
        Expect(false).ToBe(true); // Should not reach here
      }
      catch (const zjson::Error &e)
      {
        Expect(e.kind() == zjson::Error::InvalidType).ToBe(true);
      }

      try
      {
        // Should throw when trying to index string as array
        auto result = string_val[0];
        Expect(false).ToBe(true); // Should not reach here
      }
      catch (const zjson::Error &e)
      {
        Expect(e.kind() == zjson::Error::InvalidType).ToBe(true);
      }
    });

    it("should handle mixed access patterns", []()
    {
      zjson::Value root;

      // Mix traditional and dynamic construction
      root["data"] = zjson::Value::create_array();
      root["data"].add_to_array(zjson::Value("item1"));
      root["data"][1] = zjson::Value("item2"); // Dynamic indexing

      // Verify both methods work together
      Expect(root["data"][0].as_string() == "item1").ToBe(true);
      Expect(root["data"][1].as_string() == "item2").ToBe(true);
    });

    it("should handle deep nesting with mixed types", []()
    {
      zjson::Value complex;

      // Create deeply nested structure with arrays and objects
      complex["level1"]["level2"][0]["level3"] = zjson::Value("deep_value");
      complex["level1"]["array"][0] = zjson::Value(100);
      complex["level1"]["array"][1]["nested"] = zjson::Value(true);

      // Verify deep access works
      std::string deep_val = complex["level1"]["level2"][0]["level3"].as_string();
      int64_t array_val = complex["level1"]["array"][0].as_int64();
      bool nested_bool = complex["level1"]["array"][1]["nested"].as_bool();

      Expect(deep_val == "deep_value").ToBe(true);
      Expect(array_val).ToBe(100);
      Expect(nested_bool).ToBe(true);
    });

    it("should preserve structure consistency", []()
    {
      zjson::Value obj;

      // Build structure
      obj["users"][0]["name"] = zjson::Value("Alice");
      obj["users"][0]["settings"]["theme"] = zjson::Value("dark");
      obj["users"][1]["name"] = zjson::Value("Bob");

      // Verify structure integrity
      Expect(obj.is_object()).ToBe(true);
      Expect(obj["users"].is_array()).ToBe(true);
      Expect(obj["users"][0].is_object()).ToBe(true);
      Expect(obj["users"][0]["settings"].is_object()).ToBe(true);

      // Verify we can serialize it without errors
      auto serialization_result = zjson::to_value(obj);
      Expect(serialization_result.has_value()).ToBe(true);
      auto json_str_result = zjson::to_string(obj);
      Expect(json_str_result.has_value()).ToBe(true);
    });

    it("should handle safe access to non-existent paths", []()
    {
      zjson::Value root;
      root["existing"]["path"] = zjson::Value("value");

      // Access non-existent paths should return null, not throw
      const zjson::Value &missing1 = root["nonexistent"];
      const zjson::Value &missing2 = root["existing"]["nonexistent"];

      Expect(missing1.is_null()).ToBe(true);
      Expect(missing2.is_null()).ToBe(true);

      // Test that indexing wrong types throws appropriately
      try
      {
        // This should throw because we're indexing a string as object
        auto invalid = root["existing"]["path"]["invalid"];
        Expect(false).ToBe(true); // Should not reach here
      }
      catch (const zjson::Error &e)
      {
        Expect(e.kind() == zjson::Error::InvalidType).ToBe(true);
      }
    });

    it("should handle array bounds safely", []()
    {
      zjson::Value arr;
      arr[0] = zjson::Value("first");
      arr[1] = zjson::Value("second");

      // Out of bounds access on const should return null
      const zjson::Value &const_arr = arr;
      const zjson::Value &out_of_bounds = const_arr[10];
      Expect(out_of_bounds.is_null()).ToBe(true);

      // Mutable access should expand array
      arr[5] = zjson::Value("sixth");
      Expect(arr[5].as_string() == "sixth").ToBe(true);
      Expect(arr[3].is_null()).ToBe(true); // Gap filled with nulls
    });
  });
}

// ============================================================================
// SERIALIZATION/DESERIALIZATION ROUND-TRIP TESTS
// ============================================================================

void test_serialization_round_trips()
{
  describe("ZJson Serialization Round-Trip Tests", []()
  {
    it("should serialize and deserialize SimpleStruct correctly", []()
    {
      SimpleStruct original{42, "test_name"};

      // Serialize to JSON
      auto json_result = zjson::to_string(original);
      Expect(json_result.has_value()).ToBe(true);

      std::string json_str = json_result.value();
      Expect(json_str.length() > 0).ToBe(true);

      // Deserialize back
      auto struct_result = zjson::from_str<SimpleStruct>(json_str);
      Expect(struct_result.has_value()).ToBe(true);

      SimpleStruct restored = struct_result.value();
      Expect(restored.id).ToBe(original.id);
      Expect(restored.name).ToBe(original.name);
    });

    it("should handle pretty printing correctly", []()
    {
      SimpleStruct original{123, "pretty_test"};

      auto pretty_result = zjson::to_string_pretty(original);
      Expect(pretty_result.has_value()).ToBe(true);

      std::string pretty_json = pretty_result.value();
      Expect(pretty_json.length() > 0).ToBe(true);

      // Should contain newlines and spaces for formatting
      bool has_newlines = pretty_json.find('\n') != std::string::npos;
      Expect(has_newlines).ToBe(true);

      // Should still be parseable
      auto parsed_result = zjson::from_str<SimpleStruct>(pretty_json);
      Expect(parsed_result.has_value()).ToBe(true);

      SimpleStruct restored = parsed_result.value();
      Expect(restored.id).ToBe(original.id);
      Expect(restored.name).ToBe(original.name);
    });

    it("should handle optional fields serialization", []()
    {
      TestOptional original{std::optional<int>(42), std::optional<std::string>("test_string")};

      auto json_result = zjson::to_string(original);
      Expect(json_result.has_value()).ToBe(true);

      auto restored_result = zjson::from_str<TestOptional>(json_result.value());
      Expect(restored_result.has_value()).ToBe(true);

      TestOptional restored = restored_result.value();
      Expect(restored.opt_int.has_value()).ToBe(true);
      Expect(restored.opt_int.value()).ToBe(42);
      Expect(restored.opt_string.has_value()).ToBe(true);
      Expect(restored.opt_string.value()).ToBe(std::string("test_string"));
    });

    it("should handle empty optional fields", []()
    {
      TestOptional original{
          std::optional<int>(),        // empty
          std::optional<std::string>() // empty
      };

      auto json_result = zjson::to_string(original);
      Expect(json_result.has_value()).ToBe(true);

      auto restored_result = zjson::from_str<TestOptional>(json_result.value());
      Expect(restored_result.has_value()).ToBe(true);

      TestOptional restored = restored_result.value();
      Expect(restored.opt_int.has_value()).ToBe(false);
      Expect(restored.opt_string.has_value()).ToBe(false);
    });

    it("should escape a control character without a named JSON escape using the Unicode replacement character", []()
    {
      // 0x01 (SOH) has no named JSON escape (unlike backspace/formfeed/newline/cr/tab).
      // Its native byte value does not correspond to the same Unicode code point on
      // this EBCDIC platform, so echoing its raw numeric value as a generic
      // code-point escape would misrepresent it to a spec-compliant JSON reader.
      // It should be replaced with the Unicode replacement character instead.
      std::string input = "A";
      input += static_cast<char>(0x01);
      input += "B";

      SimpleStruct original{42, input};

      auto json_result = zjson::to_string(original);
      Expect(json_result.has_value()).ToBe(true);

      std::string json_str = json_result.value();
      Expect(json_str.find("\\u0001") != std::string::npos).ToBe(false);
      Expect(json_str.find("\\ufffd") != std::string::npos).ToBe(true);

      // The Unicode replacement character (U+FFFD) is outside the single-byte
      // range this decoder can represent, so re-parsing this repo's own output
      // through its own strict unescape_json_string correctly fails rather than
      // silently keeping the escape text. A standard, permissive JSON parser
      // (e.g. the SDK client's JSON.parse) would decode it without issue - this
      // decoder is intentionally stricter about what it accepts as input.
      auto restored_result = zjson::from_str<SimpleStruct>(json_str);
      Expect(restored_result.has_value()).ToBe(false);
    });

    it("should escape all EBCDIC characters (0-255) to a form with no raw control bytes", []()
    {
      // Build a string containing all EBCDIC characters from 0 to 255
      std::string all_chars;
      all_chars.reserve(256);
      for (int i = 0; i < 256; i++)
      {
        all_chars += static_cast<char>(i);
      }

      SimpleStruct original{42, all_chars};

      // Serialize to JSON
      auto json_result = zjson::to_string(original);
      Expect(json_result.has_value()).ToBe(true);

      std::string json_str = json_result.value();

      // Verify that named escapes and quote/backslash are still present.
      // Control characters without a named escape are no longer echoed via a
      // numeric code-point escape (see the test above), so an old-style
      // byte-value escape must not appear.
      bool has_escaped_chars = json_str.find("\\n") != std::string::npos;
      has_escaped_chars &= json_str.find("\\t") != std::string::npos;
      has_escaped_chars &= json_str.find("\\\"") != std::string::npos;
      has_escaped_chars &= json_str.find("\\\\") != std::string::npos;
      Expect(has_escaped_chars).ToBe(true);
      Expect(json_str.find("\\u0000") != std::string::npos).ToBe(false);
      Expect(json_str.find("\\ufffd") != std::string::npos).ToBe(true);

      // No raw control byte survives serialization: every control character,
      // named-escape or not, is represented as printable escape text (e.g. the
      // two characters backslash and 'n', not an actual newline byte).
      for (unsigned char c : json_str)
      {
        Expect(iscntrl(c)).ToBe(false);
      }

      // This text is not expected to round-trip through this repo's own strict
      // decoder - see "should reject a control character escape..." and the
      // Unicode replacement character test above for why re-parsing it
      // correctly fails rather than silently accepting lossy data back in.
    });

    it("should handle large string serialization and deserialization", []()
    {
      // Create a 16 MB string by repeating a pattern
      // This is max entry size supported by z/OS JSON parser
      // See https://www.ibm.com/support/pages/apar/OA67661
      const size_t target_size = 16 * 1024 * 1024 - 1;
      const std::string pattern = "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789abcdefghijklmnopqrstuvwxyz"; // 62 bytes
      const size_t repeats = target_size / pattern.length();

      std::string large_string;
      large_string.reserve(target_size);

      for (size_t i = 0; i < repeats; ++i)
      {
        large_string += pattern;
      }

      // Add any remaining bytes to reach exact size
      size_t remaining = target_size - large_string.length();
      if (remaining > 0)
      {
        large_string += pattern.substr(0, remaining);
      }

      Expect(large_string.length()).ToBe(target_size);

      SimpleStruct original{99, large_string};

      // Test serialization
      auto json_result = zjson::to_string(original);
      Expect(json_result.has_value()).ToBe(true);

      std::string json_str = json_result.value();

      // Test deserialization
      auto restored_result = zjson::from_str<SimpleStruct>(json_str);
      Expect(restored_result.has_value()).ToBe(true);

      SimpleStruct restored = restored_result.value();

      // Verify the ID matches
      Expect(restored.id).ToBe(original.id);

      // Verify the string length matches exactly
      Expect(restored.name.length()).ToBe(target_size);
      Expect(restored.name.size()).ToBe(large_string.size());

      // Verify the start of the string (first 100 bytes)
      std::string original_start = large_string.substr(0, 100);
      std::string restored_start = restored.name.substr(0, 100);
      Expect(restored_start).ToBe(original_start);

      // Verify the end of the string using the ORIGINAL string's length as reference
      // This ensures we're checking the actual end, not a truncated position
      std::string original_end = large_string.substr(large_string.length() - 100);
      std::string restored_end = restored.name.substr(large_string.length() - 100);
      Expect(restored_end).ToBe(original_end);
    });
  });
}

// ============================================================================
// FIELD ATTRIBUTE TESTS
// ============================================================================

struct AttributeTestStruct
{
  std::string username;
  std::string password;     // should be skipped
  int user_id;              // should be renamed to "userId"
  std::string display_name; // should be renamed to "displayName"
  int default_port;         // should have default value
};

// Test with field attributes
ZJSON_SERIALIZABLE(AttributeTestStruct, ZJSON_FIELD(AttributeTestStruct, username),
                   ZJSON_FIELD(AttributeTestStruct, password) zjson_skip(),
                   ZJSON_FIELD(AttributeTestStruct, user_id) zjson_rename("userId"),
                   ZJSON_FIELD(AttributeTestStruct, display_name) zjson_rename("displayName"),
                   ZJSON_FIELD(AttributeTestStruct, default_port) zjson_default([]()
{
  return 8080;
}));

void test_field_attributes()
{
  describe("ZJson Field Attribute Tests", []()
  {
    it("should skip fields marked with zjson_skip", []()
    {
      AttributeTestStruct original{"testuser", "secret123", 12345, "Test User", 3000};

      auto json_result = zjson::to_string(original);
      Expect(json_result.has_value()).ToBe(true);

      std::string json_str = json_result.value();

      // Password field should not appear in JSON
      bool has_password = json_str.find("password") != std::string::npos;
      Expect(has_password).ToBe(false);

      // Other fields should be present
      bool has_username = json_str.find("username") != std::string::npos;
      Expect(has_username).ToBe(true);
    });

    it("should rename fields correctly", []()
    {
      AttributeTestStruct original{"testuser", "secret", 12345, "Test User", 3000};

      auto json_result = zjson::to_string(original);
      Expect(json_result.has_value()).ToBe(true);

      std::string json_str = json_result.value();

      // Should use renamed fields
      bool has_userId = json_str.find("userId") != std::string::npos;
      bool has_displayName = json_str.find("displayName") != std::string::npos;
      Expect(has_userId).ToBe(true);
      Expect(has_displayName).ToBe(true);

      // Should not have original field names
      bool has_user_id = json_str.find("user_id") != std::string::npos;
      bool has_display_name = json_str.find("display_name") != std::string::npos;
      Expect(has_user_id).ToBe(false);
      Expect(has_display_name).ToBe(false);
    });

    it("should use default values for missing fields", []()
    {
      // JSON without default_port field
      std::string json_without_port = R"({"username":"testuser","userId":12345,"displayName":"Test User"})";

      auto result = zjson::from_str<AttributeTestStruct>(json_without_port);
      Expect(result.has_value()).ToBe(true);

      AttributeTestStruct restored = result.value();
      Expect(restored.username).ToBe(std::string("testuser"));
      Expect(restored.user_id).ToBe(12345);
      Expect(restored.display_name).ToBe(std::string("Test User"));
      Expect(restored.default_port).ToBe(8080); // Should use default value
    });
  });
}

// ============================================================================
// VALUE CONVERSION TESTS
// ============================================================================

void test_value_conversions()
{
  describe("ZJson Value Conversion Tests", []()
  {
    it("should convert typed objects to Value using to_value", []()
    {
      SimpleStruct obj{42, "test_name"};

      auto value_result = zjson::to_value(obj);
      Expect(value_result.has_value()).ToBe(true);

      zjson::Value value = value_result.value();
      Expect(value.is_object()).ToBe(true);

      // Should be able to access fields dynamically
      int64_t id = value["id"].as_int64();
      std::string name = value["name"].as_string();

      Expect(id).ToBe(42);
      Expect(name).ToBe(std::string("test_name"));
    });

    it("should convert Value back to typed objects using from_value", []()
    {
      // Create a Value manually
      zjson::Value value = zjson::Value::create_object();
      value.add_to_object("id", zjson::Value(123));
      value.add_to_object("name", zjson::Value("converted_name"));

      auto struct_result = zjson::from_value<SimpleStruct>(value);
      Expect(struct_result.has_value()).ToBe(true);

      SimpleStruct obj = struct_result.value();
      Expect(obj.id).ToBe(123);
      Expect(obj.name).ToBe(std::string("converted_name"));
    });

    it("should handle round-trip conversion: object -> Value -> object", []()
    {
      SimpleStruct original{999, "round_trip_test"};

      // Object -> Value
      auto value_result = zjson::to_value(original);
      Expect(value_result.has_value()).ToBe(true);

      // Value -> Object
      auto object_result = zjson::from_value<SimpleStruct>(value_result.value());
      Expect(object_result.has_value()).ToBe(true);

      SimpleStruct restored = object_result.value();
      Expect(restored.id).ToBe(original.id);
      Expect(restored.name).ToBe(original.name);
    });

    it("should convert basic types to Value", []()
    {
      auto int_result = zjson::to_value(42);
      Expect(int_result.has_value()).ToBe(true);
      Expect(int_result.value().as_int64()).ToBe(42);

      auto str_result = zjson::to_value(std::string("hello"));
      Expect(str_result.has_value()).ToBe(true);
      Expect(str_result.value().as_string()).ToBe(std::string("hello"));
    });

    it("should skip an unregistered field in both directions", []()
    {
      SkipUnregisteredField original{42, {7}};
      auto serialized = zjson::to_value(original);
      Expect(serialized.has_value()).ToBe(true);
      Expect(serialized.value().as_object().count("ignored")).ToBe(size_t(0));

      auto deserialized = zjson::from_value<SkipUnregisteredField>(serialized.value());
      Expect(deserialized.has_value()).ToBe(true);
      Expect(deserialized.value().id).ToBe(42);
    });

    it("should report an unskipped field without a serializer", []()
    {
      UnskippedUnregisteredField original{42, {7}};
      auto serialized = zjson::to_value(original);
      Expect(serialized.has_value()).ToBe(false);
    });

    it("should skip serialization without requiring a field serializer", []()
    {
      SkipSerializationOnly original{42, {7}};
      auto serialized = zjson::to_value(original);
      Expect(serialized.has_value()).ToBe(true);
      Expect(serialized.value().as_object().count("ignored")).ToBe(size_t(0));
    });

    it("should store integers and doubles separately", []()
    {
      // Integers stored as i64 (long long)
      zjson::Value int_val(42);
      Expect(int_val.is_integer()).ToBe(true);
      Expect(int_val.is_double()).ToBe(false);

      zjson::Value max_i64(9223372036854775807LL);
      Expect(max_i64.is_integer()).ToBe(true);

      // Floats stored as f64 (double)
      zjson::Value float_val(3.14);
      Expect(float_val.is_double()).ToBe(true);
      Expect(float_val.is_integer()).ToBe(false);
    });

    it("should preserve i64 precision through JSON round-trip", []()
    {
      long long max_i64 = 9223372036854775807LL;
      zjson::Value obj = zjson::Value::create_object();
      obj["maxInt"] = zjson::Value(max_i64);

      auto json_result = zjson::to_string(obj);
      Expect(json_result.has_value()).ToBe(true);

      auto parsed = zjson::from_str(json_result.value());
      Expect(parsed.has_value()).ToBe(true);
      Expect(parsed.value()["maxInt"].is_integer()).ToBe(true);
      Expect(parsed.value()["maxInt"].as_int64()).ToBe(max_i64);
    });

    it("should handle u64 within i64 range as integer", []()
    {
      // u64 values <= i64::MAX stored as integer
      zjson::Value small_u64(1000ULL);
      Expect(small_u64.is_integer()).ToBe(true);
      Expect(small_u64.as_uint64()).ToBe(1000ULL);

      // u64 values > i64::MAX stored as f64 (precision loss acceptable)
      zjson::Value large_u64(18446744073709551615ULL);
      Expect(large_u64.is_double()).ToBe(true);
    });

    it("should support as_i64/as_u64/as_f64 conversions", []()
    {
      // i64 -> f64 conversion
      zjson::Value int_val(42);
      Expect(int_val.as_double()).ToBe(42.0);

      // f64 -> i64 conversion (whole numbers only)
      zjson::Value whole_double(100.0);
      Expect(whole_double.as_int64()).ToBe(100);

      // f64 with fraction -> i64 should error
      zjson::Value fractional(3.14);
      try
      {
        fractional.as_int64();
        Expect(false).ToBe(true);
      }
      catch (const zjson::Error &e)
      {
        Expect(e.kind() == zjson::Error::InvalidValue).ToBe(true);
      }
    });
  });
}

// ============================================================================
// COMPLEX NESTED STRUCTURE TESTS
// ============================================================================

struct Address
{
  std::string street;
  std::string city;
  std::string country;
};

struct Person
{
  std::string name;
  int age;
  Address address;
};

struct Company
{
  std::string name;
  std::vector<Person> employees;
  Address headquarters;
};

ZJSON_DERIVE(Address, street, city, country);
ZJSON_DERIVE(Person, name, age, address);
ZJSON_DERIVE(Company, name, employees, headquarters);

void test_complex_nested_structures()
{
  describe("ZJson Complex Nested Structure Tests", []()
  {
    it("should serialize and deserialize nested objects", []()
    {
      Address addr{"123 Main St", "Boston", "USA"};
      Person person{"John Doe", 30, addr};

      auto json_result = zjson::to_string(person);
      Expect(json_result.has_value()).ToBe(true);

      auto person_result = zjson::from_str<Person>(json_result.value());
      Expect(person_result.has_value()).ToBe(true);

      Person restored = person_result.value();
      Expect(restored.name).ToBe(std::string("John Doe"));
      Expect(restored.age).ToBe(30);
      Expect(restored.address.street).ToBe(std::string("123 Main St"));
      Expect(restored.address.city).ToBe(std::string("Boston"));
      Expect(restored.address.country).ToBe(std::string("USA"));
    });

    it("should handle arrays of complex objects", []()
    {
      Address hq{"456 Corporate Blvd", "Seattle", "USA"};

      Person emp1{"Alice Smith", 28, {"111 First St", "Boston", "USA"}};
      Person emp2{"Bob Jones", 35, {"222 Second Ave", "Seattle", "USA"}};

      Company company{"Tech Corp", {emp1, emp2}, hq};

      auto json_result = zjson::to_string_pretty(company);
      Expect(json_result.has_value()).ToBe(true);

      auto company_result = zjson::from_str<Company>(json_result.value());
      Expect(company_result.has_value()).ToBe(true);

      Company restored = company_result.value();
      Expect(restored.name).ToBe(std::string("Tech Corp"));
      Expect(restored.employees.size()).ToBe(2);
      Expect(restored.employees[0].name).ToBe(std::string("Alice Smith"));
      Expect(restored.employees[1].name).ToBe(std::string("Bob Jones"));
      Expect(restored.headquarters.city).ToBe(std::string("Seattle"));
    });

    it("should support deep nested access with dynamic indexing", []()
    {
      Company company{"Test Corp", {{"John", 30, {"123 St", "Boston", "USA"}}}, {"456 HQ Ave", "Seattle", "USA"}};

      auto value_result = zjson::to_value(company);
      Expect(value_result.has_value()).ToBe(true);

      zjson::Value root = value_result.value();

      // Deep chained access
      std::string emp_name = root["employees"][0]["name"].as_string();
      std::string emp_city = root["employees"][0]["address"]["city"].as_string();
      std::string hq_country = root["headquarters"]["country"].as_string();

      Expect(emp_name).ToBe(std::string("John"));
      Expect(emp_city).ToBe(std::string("Boston"));
      Expect(hq_country).ToBe(std::string("USA"));
    });
  });
}

// ============================================================================
// LARGE STRUCT TESTS
// ============================================================================

struct LargeStruct
{
  std::string f1, f2, f3, f4, f5, f6, f7, f8, f9, f10;
  std::string f11, f12, f13, f14, f15, f16, f17, f18, f19, f20;
  int f21, f22, f23, f24, f25, f26, f27, f28, f29, f30, f31, f32;
};

// Test the new macros we added (17-32 fields)
ZJSON_DERIVE(LargeStruct, f1, f2, f3, f4, f5, f6, f7, f8, f9, f10, f11, f12, f13, f14, f15, f16, f17, f18, f19, f20,
             f21, f22, f23, f24, f25, f26, f27, f28, f29, f30, f31, f32);

void test_large_struct_support()
{
  describe("ZJson Large Struct Support Tests", []()
  {
    it("should handle structs with 32 fields using new macros", []()
    {
      LargeStruct large;
      large.f1 = "field1";
      large.f2 = "field2";
      large.f3 = "field3";
      large.f4 = "field4";
      large.f5 = "field5";
      large.f6 = "field6";
      large.f7 = "field7";
      large.f8 = "field8";
      large.f9 = "field9";
      large.f10 = "field10";
      large.f11 = "field11";
      large.f12 = "field12";
      large.f13 = "field13";
      large.f14 = "field14";
      large.f15 = "field15";
      large.f16 = "field16";
      large.f17 = "field17";
      large.f18 = "field18";
      large.f19 = "field19";
      large.f20 = "field20";
      large.f21 = 21;
      large.f22 = 22;
      large.f23 = 23;
      large.f24 = 24;
      large.f25 = 25;
      large.f26 = 26;
      large.f27 = 27;
      large.f28 = 28;
      large.f29 = 29;
      large.f30 = 30;
      large.f31 = 31;
      large.f32 = 32;

      // Test serialization
      auto json_result = zjson::to_string(large);
      Expect(json_result.has_value()).ToBe(true);

      std::string json_str = json_result.value();

      // Should contain all fields
      bool has_f1 = json_str.find("f1") != std::string::npos;
      bool has_f17 = json_str.find("f17") != std::string::npos; // Test new macro range
      bool has_f32 = json_str.find("f32") != std::string::npos;

      Expect(has_f1).ToBe(true);
      Expect(has_f17).ToBe(true);
      Expect(has_f32).ToBe(true);

      // Test deserialization
      auto restored_result = zjson::from_str<LargeStruct>(json_str);
      Expect(restored_result.has_value()).ToBe(true);

      LargeStruct restored = restored_result.value();
      Expect(restored.f1).ToBe(std::string("field1"));
      Expect(restored.f17).ToBe(std::string("field17")); // Test new macro range
      Expect(restored.f32).ToBe(32);
    });

    it("should validate serializable trait works for large structs", []()
    {
      bool is_serializable = zjson::Serializable<LargeStruct>::value;
      bool is_deserializable = zjson::Deserializable<LargeStruct>::value;

      Expect(is_serializable).ToBe(true);
      Expect(is_deserializable).ToBe(true);
    });
  });
}

// ============================================================================
// CONTAINER ATTRIBUTE TESTS
// ============================================================================

struct CamelCaseStruct
{
  std::string user_name;
  int user_id;
  std::string display_name;
};

struct PascalCaseStruct
{
  std::string first_name;
  std::string last_name;
  int user_count;
};

struct KebabCaseStruct
{
  std::string user_name;
  std::string email_address;
  int session_timeout;
};

struct UppercaseStruct
{
  std::string user_name;
  std::string api_key;
  int max_connections;
};

struct LowercaseStruct
{
  std::string USER_NAME;
  std::string API_KEY;
  int MAX_CONNECTIONS;
};

struct SnakeCaseStruct
{
  std::string userName;
  std::string emailAddress;
  int sessionTimeout;
};

struct ScreamingSnakeCaseStruct
{
  std::string user_name;
  std::string api_key;
  int max_connections;
};

struct ScreamingKebabCaseStruct
{
  std::string user_name;
  std::string api_key;
  int max_connections;
};

struct StrictValidationStruct
{
  std::string name;
  int id;
};

struct RenamedStrictStruct
{
  std::string user_name;
};

// Register structs with different rename_all styles
// Note: Container attributes must be declared before ZJSON_DERIVE to avoid template specialization errors
ZJSON_RENAME_ALL(CamelCaseStruct, camelCase);
ZJSON_DERIVE(CamelCaseStruct, user_name, user_id, display_name);

ZJSON_RENAME_ALL(PascalCaseStruct, PascalCase);
ZJSON_DERIVE(PascalCaseStruct, first_name, last_name, user_count);

ZJSON_RENAME_ALL(KebabCaseStruct, kebab_case);
ZJSON_DERIVE(KebabCaseStruct, user_name, email_address, session_timeout);

ZJSON_RENAME_ALL(UppercaseStruct, UPPERCASE);
ZJSON_DERIVE(UppercaseStruct, user_name, api_key, max_connections);

ZJSON_RENAME_ALL(LowercaseStruct, lowercase);
ZJSON_DERIVE(LowercaseStruct, USER_NAME, API_KEY, MAX_CONNECTIONS);

ZJSON_RENAME_ALL(SnakeCaseStruct, snake_case);
ZJSON_DERIVE(SnakeCaseStruct, userName, emailAddress, sessionTimeout);

ZJSON_RENAME_ALL(ScreamingSnakeCaseStruct, SCREAMING_SNAKE_CASE);
ZJSON_DERIVE(ScreamingSnakeCaseStruct, user_name, api_key, max_connections);

ZJSON_RENAME_ALL(ScreamingKebabCaseStruct, SCREAMING_KEBAB_CASE);
ZJSON_DERIVE(ScreamingKebabCaseStruct, user_name, api_key, max_connections);

ZJSON_DENY_UNKNOWN_FIELDS(StrictValidationStruct);
ZJSON_DERIVE(StrictValidationStruct, name, id);

ZJSON_RENAME_ALL(RenamedStrictStruct, camelCase);
ZJSON_DENY_UNKNOWN_FIELDS(RenamedStrictStruct);
ZJSON_DERIVE(RenamedStrictStruct, user_name);

static_assert(zjson::detail::StructConfig<RenamedStrictStruct>::rename_all_case ==
              zjson::attributes::RenameAll::camelCase);
static_assert(zjson::detail::StructConfig<RenamedStrictStruct>::deny_unknown_fields);
static_assert(zjson::detail::StructConfig<SimpleStruct>::rename_all_case == zjson::attributes::RenameAll::none);
static_assert(!zjson::detail::StructConfig<SimpleStruct>::deny_unknown_fields);

void test_container_attributes()
{
  describe("ZJson Container Attribute Tests", []()
  {
    it("should apply camelCase rename_all transformation", []()
    {
      CamelCaseStruct obj{"john_doe", 123, "John Doe"};

      auto json_result = zjson::to_string(obj);
      Expect(json_result.has_value()).ToBe(true);

      std::string json_str = json_result.value();

      // Should use camelCase field names
      bool has_userName = json_str.find("userName") != std::string::npos;
      bool has_userId = json_str.find("userId") != std::string::npos;
      bool has_displayName = json_str.find("displayName") != std::string::npos;

      Expect(has_userName).ToBe(true);
      Expect(has_userId).ToBe(true);
      Expect(has_displayName).ToBe(true);

      // Should NOT use original field names
      bool has_user_name = json_str.find("user_name") != std::string::npos;
      bool has_user_id = json_str.find("user_id") != std::string::npos;
      bool has_display_name = json_str.find("display_name") != std::string::npos;

      Expect(has_user_name).ToBe(false);
      Expect(has_user_id).ToBe(false);
      Expect(has_display_name).ToBe(false);
    });

    it("should apply PascalCase rename_all transformation", []()
    {
      PascalCaseStruct obj{"John", "Doe", 5};

      auto json_result = zjson::to_string(obj);
      Expect(json_result.has_value()).ToBe(true);

      std::string json_str = json_result.value();

      // Should use PascalCase field names
      bool has_FirstName = json_str.find("FirstName") != std::string::npos;
      bool has_LastName = json_str.find("LastName") != std::string::npos;
      bool has_UserCount = json_str.find("UserCount") != std::string::npos;

      Expect(has_FirstName).ToBe(true);
      Expect(has_LastName).ToBe(true);
      Expect(has_UserCount).ToBe(true);
    });

    it("should apply kebab-case rename_all transformation", []()
    {
      KebabCaseStruct obj{"test_user", "test@example.com", 3600};

      auto json_result = zjson::to_string(obj);
      Expect(json_result.has_value()).ToBe(true);

      std::string json_str = json_result.value();

      // Should use kebab-case field names
      bool has_user_name = json_str.find("user-name") != std::string::npos;
      bool has_email_address = json_str.find("email-address") != std::string::npos;
      bool has_session_timeout = json_str.find("session-timeout") != std::string::npos;

      Expect(has_user_name).ToBe(true);
      Expect(has_email_address).ToBe(true);
      Expect(has_session_timeout).ToBe(true);
    });

    it("should apply UPPERCASE rename_all transformation", []()
    {
      UppercaseStruct obj{"testuser", "secret123", 100};

      auto json_result = zjson::to_string(obj);
      Expect(json_result.has_value()).ToBe(true);

      std::string json_str = json_result.value();

      // Should use UPPERCASE field names
      bool has_USER_NAME = json_str.find("USER_NAME") != std::string::npos;
      bool has_API_KEY = json_str.find("API_KEY") != std::string::npos;
      bool has_MAX_CONNECTIONS = json_str.find("MAX_CONNECTIONS") != std::string::npos;

      Expect(has_USER_NAME).ToBe(true);
      Expect(has_API_KEY).ToBe(true);
      Expect(has_MAX_CONNECTIONS).ToBe(true);
    });

    it("should apply lowercase rename_all transformation", []()
    {
      LowercaseStruct obj{"TESTUSER", "SECRET123", 100};

      auto json_result = zjson::to_string(obj);
      Expect(json_result.has_value()).ToBe(true);

      std::string json_str = json_result.value();

      // Should use lowercase field names
      bool has_user_name = json_str.find("user_name") != std::string::npos;
      bool has_api_key = json_str.find("api_key") != std::string::npos;
      bool has_max_connections = json_str.find("max_connections") != std::string::npos;

      Expect(has_user_name).ToBe(true);
      Expect(has_api_key).ToBe(true);
      Expect(has_max_connections).ToBe(true);
    });

    it("should apply snake_case rename_all transformation", []()
    {
      SnakeCaseStruct obj{"testUser", "testEmail", 1800};

      auto json_result = zjson::to_string(obj);
      Expect(json_result.has_value()).ToBe(true);

      std::string json_str = json_result.value();

      // Should use snake_case field names (camelCase -> snake_case)
      bool has_user_name = json_str.find("userName") != std::string::npos;
      bool has_email_address = json_str.find("emailAddress") != std::string::npos;
      bool has_session_timeout = json_str.find("sessionTimeout") != std::string::npos;

      Expect(has_user_name).ToBe(true);
      Expect(has_email_address).ToBe(true);
      Expect(has_session_timeout).ToBe(true);
    });

    it("should apply SCREAMING_SNAKE_CASE rename_all transformation", []()
    {
      ScreamingSnakeCaseStruct obj{"testuser", "secret", 50};

      auto json_result = zjson::to_string(obj);
      Expect(json_result.has_value()).ToBe(true);

      std::string json_str = json_result.value();

      // Should use SCREAMING_SNAKE_CASE field names
      bool has_USER_NAME = json_str.find("USER_NAME") != std::string::npos;
      bool has_API_KEY = json_str.find("API_KEY") != std::string::npos;
      bool has_MAX_CONNECTIONS = json_str.find("MAX_CONNECTIONS") != std::string::npos;

      Expect(has_USER_NAME).ToBe(true);
      Expect(has_API_KEY).ToBe(true);
      Expect(has_MAX_CONNECTIONS).ToBe(true);
    });

    it("should apply SCREAMING-KEBAB-CASE rename_all transformation", []()
    {
      ScreamingKebabCaseStruct obj{"testuser", "secret", 50};

      auto json_result = zjson::to_string(obj);
      Expect(json_result.has_value()).ToBe(true);

      std::string json_str = json_result.value();

      // Should use SCREAMING-KEBAB-CASE field names
      bool has_USER_NAME = json_str.find("USER-NAME") != std::string::npos;
      bool has_API_KEY = json_str.find("API-KEY") != std::string::npos;
      bool has_MAX_CONNECTIONS = json_str.find("MAX-CONNECTIONS") != std::string::npos;

      Expect(has_USER_NAME).ToBe(true);
      Expect(has_API_KEY).ToBe(true);
      Expect(has_MAX_CONNECTIONS).ToBe(true);
    });

    it("should deserialize with renamed fields correctly", []()
    {
      // Test round-trip with camelCase
      std::string camel_json = R"({"userName":"test","userId":42,"displayName":"Test User"})";

      auto result = zjson::from_str<CamelCaseStruct>(camel_json);
      Expect(result.has_value()).ToBe(true);

      CamelCaseStruct restored = result.value();
      Expect(restored.user_name).ToBe(std::string("test"));
      Expect(restored.user_id).ToBe(42);
      Expect(restored.display_name).ToBe(std::string("Test User"));
    });

    it("should deny unknown fields when ZJSON_DENY_UNKNOWN_FIELDS is used", []()
    {
      // Valid JSON should work
      std::string valid_json = R"({"name":"test","id":123})";
      auto valid_result = zjson::from_str<StrictValidationStruct>(valid_json);
      Expect(valid_result.has_value()).ToBe(true);

      // JSON with extra fields should fail
      std::string invalid_json = R"({"name":"test","id":123,"extra_field":"should_fail"})";
      auto invalid_result = zjson::from_str<StrictValidationStruct>(invalid_json);
      Expect(invalid_result.has_value()).ToBe(false);

      // Should be unknown field error
      auto error = invalid_result.error();
      Expect(error.kind() == zjson::Error::UnknownField).ToBe(true);
    });

    it("should combine rename_all with deny_unknown_fields", []()
    {
      auto serialized = zjson::to_value(RenamedStrictStruct{"test"});
      Expect(serialized.has_value()).ToBe(true);
      Expect(serialized.value().as_object().count("userName")).ToBe(size_t(1));
      Expect(serialized.value().as_object().count("user_name")).ToBe(size_t(0));

      auto valid_result = zjson::from_str<RenamedStrictStruct>(R"({"userName":"test"})");
      Expect(valid_result.has_value()).ToBe(true);
      Expect(valid_result.value().user_name).ToBe(std::string("test"));

      auto invalid_result = zjson::from_str<RenamedStrictStruct>(R"({"userName":"test","extra":1})");
      Expect(invalid_result.has_value()).ToBe(false);
      Expect(invalid_result.error().kind() == zjson::Error::UnknownField).ToBe(true);
    });

    it("should allow unknown fields by default (when ZJSON_DENY_UNKNOWN_FIELDS not used)", []()
    {
      // Regular struct without ZJSON_DENY_UNKNOWN_FIELDS should accept extra fields
      // Using SimpleStruct which has no ZJSON_RENAME_ALL, so uses original field names
      std::string json_with_extra = R"({"id":123,"name":"test","extra_field":"ignored"})";
      auto result = zjson::from_str<SimpleStruct>(json_with_extra);
      Expect(result.has_value()).ToBe(true);

      SimpleStruct restored = result.value();
      Expect(restored.id).ToBe(123);
      Expect(restored.name).ToBe(std::string("test"));
    });

    it("should use 'none' transformation by default (no rename_all)", []()
    {
      // SimpleStruct doesn't have ZJSON_RENAME_ALL, so should use original field names
      SimpleStruct obj{42, "test"};

      auto json_result = zjson::to_string(obj);
      Expect(json_result.has_value()).ToBe(true);

      std::string json_str = json_result.value();

      // Should use original field names (no transformation)
      bool has_id = json_str.find("id") != std::string::npos;
      bool has_name = json_str.find("name") != std::string::npos;

      Expect(has_id).ToBe(true);
      Expect(has_name).ToBe(true);
    });
  });
}

// ============================================================================
// STRUCT FLATTENING TESTS
// ============================================================================

void test_struct_flattening()
{
  describe("ZJson Struct Flattening Tests", []()
  {
    it("should flatten nested struct fields into parent", []()
    {
      DatabaseConfig config;
      config.connection.host = "localhost";
      config.connection.port = 5432;
      config.database_name = "myapp";
      config.ssl_enabled = true;

      auto json_result = zjson::to_string(config);
      Expect(json_result.has_value()).ToBe(true);

      std::string json_str = json_result.value();

      // Should contain flattened fields directly, not nested
      bool has_host = json_str.find("\"host\"") != std::string::npos;
      bool has_port = json_str.find("\"port\"") != std::string::npos;
      bool has_database_name = json_str.find("\"database_name\"") != std::string::npos;
      bool has_ssl_enabled = json_str.find("\"ssl_enabled\"") != std::string::npos;

      // Should NOT contain nested "connection" object
      bool has_connection = json_str.find("\"connection\"") != std::string::npos;

      Expect(has_host).ToBe(true);
      Expect(has_port).ToBe(true);
      Expect(has_database_name).ToBe(true);
      Expect(has_ssl_enabled).ToBe(true);
      Expect(has_connection).ToBe(false);
    });

    it("should deserialize flattened JSON back to nested struct", []()
    {
      std::string json_str =
          R"({"host":"db.example.com","port":3306,"database_name":"production","ssl_enabled":false})";

      auto result = zjson::from_str<DatabaseConfig>(json_str);
      Expect(result.has_value()).ToBe(true);

      DatabaseConfig config = result.value();
      Expect(config.connection.host).ToBe(std::string("db.example.com"));
      Expect(config.connection.port).ToBe(3306);
      Expect(config.database_name).ToBe(std::string("production"));
      Expect(config.ssl_enabled).ToBe(false);
    });

    it("should handle round-trip serialization correctly", []()
    {
      DatabaseConfig original;
      original.connection.host = "test.db";
      original.connection.port = 9999;
      original.database_name = "test_db";
      original.ssl_enabled = true;

      // Serialize
      auto json_result = zjson::to_string(original);
      Expect(json_result.has_value()).ToBe(true);

      // Deserialize
      auto restored_result = zjson::from_str<DatabaseConfig>(json_result.value());
      Expect(restored_result.has_value()).ToBe(true);

      DatabaseConfig restored = restored_result.value();
      Expect(restored.connection.host).ToBe(original.connection.host);
      Expect(restored.connection.port).ToBe(original.connection.port);
      Expect(restored.database_name).ToBe(original.database_name);
      Expect(restored.ssl_enabled).ToBe(original.ssl_enabled);
    });
  });
}

void zjson_tests()
{
  // Core API and type system tests - focused on API functionality
  test_type_traits();
  test_value_types();
  test_optional_types();
  test_expected_types();
  test_basic_api();
  test_dynamic_access_edge_cases();
  test_serialization_round_trips();
  test_field_attributes();
  test_value_conversions();
  test_complex_nested_structures();
  test_large_struct_support();
  test_container_attributes();
  test_struct_flattening();
}
