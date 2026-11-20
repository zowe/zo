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

#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>
#include <cstring>
#include <unistd.h> // For __e2a_s function

#include "ztest.hpp"
#include "../zbase64.h"

using namespace ztst;

// Helper function to convert EBCDIC std::string to ASCII
std::string e2a_convert(const std::string &ebcdic_str)
{
  std::string ascii_str = ebcdic_str;
  size_t converted_len = 0;
  converted_len = __e2a_s(const_cast<char *>(ascii_str.c_str()));
  return ascii_str;
}

void zbase64_tests()
{
  describe("zb64 encode tests", []() -> void
  {
    it("should encode empty std::string", []() -> void
    {
      std::string input = e2a_convert("");
      std::vector<char> result = zbase64::encode(input.c_str(), input.length());
      std::string result_str(result.begin(), result.end());
      Expect(result_str).ToBe("");
    });

    it("should encode 'Hello World'", []() -> void
    {
      std::string input = e2a_convert("Hello World");
      std::vector<char> result = zbase64::encode(input.c_str(), input.length());
      std::string result_str(result.begin(), result.end());
      Expect(result_str).ToBe("SGVsbG8gV29ybGQ=");
    });

    it("should encode 'f'", []() -> void
    {
      std::string input = e2a_convert("f");
      std::vector<char> result = zbase64::encode(input.c_str(), input.length());
      std::string result_str(result.begin(), result.end());
      Expect(result_str).ToBe("Zg==");
    });

    it("should encode 'fo'", []() -> void
    {
      std::string input = e2a_convert("fo");
      std::vector<char> result = zbase64::encode(input.c_str(), input.length());
      std::string result_str(result.begin(), result.end());
      Expect(result_str).ToBe("Zm8=");
    });

    it("should encode 'foo'", []() -> void
    {
      std::string input = e2a_convert("foo");
      std::vector<char> result = zbase64::encode(input.c_str(), input.length());
      std::string result_str(result.begin(), result.end());
      Expect(result_str).ToBe("Zm9v");
    });

    it("should encode 'foob'", []() -> void
    {
      std::string input = e2a_convert("foob");
      std::vector<char> result = zbase64::encode(input.c_str(), input.length());
      std::string result_str(result.begin(), result.end());
      Expect(result_str).ToBe("Zm9vYg==");
    });

    it("should encode 'fooba'", []() -> void
    {
      std::string input = e2a_convert("fooba");
      std::vector<char> result = zbase64::encode(input.c_str(), input.length());
      std::string result_str(result.begin(), result.end());
      Expect(result_str).ToBe("Zm9vYmE=");
    });

    it("should encode 'foobar'", []() -> void
    {
      std::string input = e2a_convert("foobar");
      std::vector<char> result = zbase64::encode(input.c_str(), input.length());
      std::string result_str(result.begin(), result.end());
      Expect(result_str).ToBe("Zm9vYmFy");
    });

    it("should encode binary data with null bytes", []() -> void
    {
      std::vector<char> input = {0x00, 0x01, 0x02, 0x03, 0x04, 0x05};
      std::vector<char> result = zbase64::encode(input.data(), input.size());
      std::string result_str(result.begin(), result.end());
      Expect(result_str).ToBe("AAECAwQF");
    });

    it("should encode all 256 byte values", []() -> void
    {
      std::vector<char> input;
      input.reserve(256);
      for (int i = 0; i < 256; i++)
      {
        input.push_back(static_cast<char>(i));
      }
      std::vector<char> result = zbase64::encode(input.data(), input.size());
      size_t expected_size = zbase64::encoded_size(256);
      Expect(result.size()).ToBe(expected_size);
    });

    it("should handle large input", []() -> void
    {
      std::string input(10000, 'A');
      // Convert the entire std::string to ASCII
      size_t converted_len = 0;
      converted_len = __e2a_s(const_cast<char *>(input.c_str()));

      std::vector<char> result = zbase64::encode(input.c_str(), input.length());
      size_t expected_size = zbase64::encoded_size(10000);
      Expect(result.size()).ToBe(expected_size);
    });
  });

  describe("zb64 decode tests", []() -> void
  {
    it("should decode empty std::string", []() -> void
    {
      std::string input = "";
      std::vector<char> result = zbase64::decode(input.c_str(), input.length());
      std::string result_str(result.begin(), result.end());
      Expect(result_str).ToBe(e2a_convert(""));
    });

    it("should decode 'SGVsbG8gV29ybGQ=' to 'Hello World'", []() -> void
    {
      std::string input = "SGVsbG8gV29ybGQ=";
      std::vector<char> result = zbase64::decode(input.c_str(), input.length());
      std::string result_str(result.begin(), result.end());
      Expect(result_str).ToBe(e2a_convert("Hello World"));
    });

    it("should decode 'Zg==' to 'f'", []() -> void
    {
      std::string input = "Zg==";
      std::vector<char> result = zbase64::decode(input.c_str(), input.length());
      std::string result_str(result.begin(), result.end());
      Expect(result_str).ToBe(e2a_convert("f"));
    });

    it("should decode 'Zm8=' to 'fo'", []() -> void
    {
      std::string input = "Zm8=";
      std::vector<char> result = zbase64::decode(input.c_str(), input.length());
      std::string result_str(result.begin(), result.end());
      Expect(result_str).ToBe(e2a_convert("fo"));
    });

    it("should decode 'Zm9v' to 'foo'", []() -> void
    {
      std::string input = "Zm9v";
      std::vector<char> result = zbase64::decode(input.c_str(), input.length());
      std::string result_str(result.begin(), result.end());
      Expect(result_str).ToBe(e2a_convert("foo"));
    });

    it("should decode 'Zm9vYg==' to 'foob'", []() -> void
    {
      std::string input = "Zm9vYg==";
      std::vector<char> result = zbase64::decode(input.c_str(), input.length());
      std::string result_str(result.begin(), result.end());
      Expect(result_str).ToBe(e2a_convert("foob"));
    });

    it("should decode 'Zm9vYmE=' to 'fooba'", []() -> void
    {
      std::string input = "Zm9vYmE=";
      std::vector<char> result = zbase64::decode(input.c_str(), input.length());
      std::string result_str(result.begin(), result.end());
      Expect(result_str).ToBe(e2a_convert("fooba"));
    });

    it("should decode 'Zm9vYmFy' to 'foobar'", []() -> void
    {
      std::string input = "Zm9vYmFy";
      std::vector<char> result = zbase64::decode(input.c_str(), input.length());
      std::string result_str(result.begin(), result.end());
      Expect(result_str).ToBe(e2a_convert("foobar"));
    });

    it("should handle binary data with null bytes", []() -> void
    {
      std::string input = "AAECAwQF";
      std::vector<char> result = zbase64::decode(input.c_str(), input.length());
      std::vector<char> expected = {0x00, 0x01, 0x02, 0x03, 0x04, 0x05};
      Expect(result.size()).ToBe(expected.size());
      for (size_t i = 0; i < expected.size(); i++)
      {
        Expect(static_cast<unsigned char>(result[i])).ToBe(static_cast<unsigned char>(expected[i]));
      }
    });

    it("should handle invalid input gracefully", []() -> void
    {
      std::string input = e2a_convert("ABC@"); // @ is not valid base64
      try
      {
        std::vector<char> result = zbase64::decode(input.c_str(), input.length());
        // Should not reach here
        Expect(false).ToBe(true);
      }
      catch (const std::invalid_argument &)
      {
        // Expected behavior - invalid input throws exception
        Expect(true).ToBe(true);
      }
    });

    it("should handle invalid input length", []() -> void
    {
      std::string input = e2a_convert("ABC"); // Not multiple of 4
      try
      {
        std::vector<char> result = zbase64::decode(input.c_str(), input.length());
        // Should not reach here
        Expect(false).ToBe(true);
      }
      catch (const std::invalid_argument &)
      {
        // Expected behavior - invalid input throws exception
        Expect(true).ToBe(true);
      }
    });
  });

  describe("zb64 round-trip tests", []() -> void
  {
    it("should encode and decode 'Hello World' correctly", []() -> void
    {
      std::string original = e2a_convert("Hello World");

      // Encode
      std::vector<char> encoded = zbase64::encode(original.c_str(), original.length());

      // Decode
      std::vector<char> decoded = zbase64::decode(encoded.data(), encoded.size());
      std::string decoded_str(decoded.begin(), decoded.end());

      Expect(decoded_str).ToBe(original);
    });

    it("should handle empty std::string round-trip", []() -> void
    {
      std::string original = e2a_convert("");

      // Encode
      std::vector<char> encoded = zbase64::encode(original.c_str(), original.length());

      // Decode
      std::vector<char> decoded = zbase64::decode(encoded.data(), encoded.size());
      std::string decoded_str(decoded.begin(), decoded.end());

      Expect(decoded_str).ToBe(original);
    });

    it("should handle single character round-trip", []() -> void
    {
      std::string original = e2a_convert("A");

      // Encode
      std::vector<char> encoded = zbase64::encode(original.c_str(), original.length());

      // Decode
      std::vector<char> decoded = zbase64::decode(encoded.data(), encoded.size());
      std::string decoded_str(decoded.begin(), decoded.end());

      Expect(decoded_str).ToBe(original);
    });

    it("should handle binary data round-trip", []() -> void
    {
      std::vector<char> original;
      original.reserve(256);
      for (int i = 0; i < 256; i++)
      {
        original.push_back(static_cast<char>(i));
      }

      // Encode
      std::vector<char> encoded = zbase64::encode(original.data(), original.size());

      // Decode
      std::vector<char> decoded = zbase64::decode(encoded.data(), encoded.size());

      Expect(decoded.size()).ToBe(original.size());
      for (size_t i = 0; i < original.size(); i++)
      {
        Expect(static_cast<unsigned char>(decoded[i])).ToBe(static_cast<unsigned char>(original[i]));
      }
    });

    it("should handle large data round-trip", []() -> void
    {
      std::string original(1000, 'X');
      original += e2a_convert("Hello World");
      original += std::string(1000, 'Y');

      // Convert the entire std::string to ASCII
      size_t converted_len = 0;
      converted_len = __e2a_s(const_cast<char *>(original.c_str()));

      // Encode
      std::vector<char> encoded = zbase64::encode(original.c_str(), original.length());

      // Decode
      std::vector<char> decoded = zbase64::decode(encoded.data(), encoded.size());
      std::string decoded_str(decoded.begin(), decoded.end());

      Expect(decoded_str).ToBe(original);
    });

    it("should handle special characters round-trip", []() -> void
    {
      std::string original = e2a_convert("!@#$%^&*()_+-=[]{}|;':\",./<>?");

      // Encode
      std::vector<char> encoded = zbase64::encode(original.c_str(), original.length());

      // Decode
      std::vector<char> decoded = zbase64::decode(encoded.data(), encoded.size());
      std::string decoded_str(decoded.begin(), decoded.end());

      Expect(decoded_str).ToBe(original);
    });

    it("should handle binary data with control characters round-trip", []() -> void
    {
      // Test with binary data containing control characters
      std::string original = e2a_convert("Hello");
      original += std::string("\x00\x01\x02\x03\x04\x05", 6);
      original += e2a_convert("World");

      // Encode
      std::vector<char> encoded = zbase64::encode(original.c_str(), original.length());

      // Decode
      std::vector<char> decoded = zbase64::decode(encoded.data(), encoded.size());
      std::string decoded_str(decoded.begin(), decoded.end());

      Expect(decoded_str).ToBe(original);
    });
  });

  describe("zb64 performance and edge cases", []() -> void
  {
    it("should handle C API correctly", []() -> void
    {
      const char *input_ebcdic = "Hello World";
      std::string input_ascii = e2a_convert(input_ebcdic);
      size_t input_len = input_ascii.length();

      // Encode
      std::vector<char> encoded = zbase64::encode(input_ascii.c_str(), input_len);

      // Decode
      std::vector<char> decoded = zbase64::decode(encoded.data(), encoded.size());
      std::string result(decoded.begin(), decoded.end());

      Expect(result).ToBe(input_ascii);
    });

    it("should calculate encoded size correctly", []() -> void
    {
      // Test various input sizes to verify encoded size calculation
      for (int input_size = 0; input_size <= 6; input_size++)
      {
        std::string input(input_size, 'A');
        // Convert to ASCII
        size_t converted_len = 0;
        converted_len = __e2a_s(const_cast<char *>(input.c_str()));

        std::vector<char> result = zbase64::encode(input.c_str(), input.length());

        // Expected size is (input_size + 2) / 3 * 4
        size_t expected_size = zbase64::encoded_size(input_size);
        Expect(result.size()).ToBe(expected_size);
      }
    });

    it("should handle null bytes in middle of data", []() -> void
    {
      std::vector<char> original = {'A', 'B', 0x00, 'C', 'D'};
      // Convert ASCII characters to ASCII (A, B, C, D)
      for (size_t i = 0; i < original.size(); i++)
      {
        if (original[i] != 0x00)
        {
          std::string temp(1, original[i]);
          size_t converted_len = 0;
          converted_len = __e2a_s(const_cast<char *>(temp.c_str()));
          original[i] = temp[0];
        }
      }

      // Encode
      std::vector<char> encoded = zbase64::encode(original.data(), original.size());

      // Decode
      std::vector<char> decoded = zbase64::decode(encoded.data(), encoded.size());

      Expect(decoded.size()).ToBe(original.size());
      for (size_t i = 0; i < original.size(); i++)
      {
        Expect(decoded[i]).ToBe(original[i]);
      }
    });
  });
}
