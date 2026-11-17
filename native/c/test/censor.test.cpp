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

#include <string>

#include "censor.test.hpp"
#include "ztest.hpp"
#include "../censor.hpp"

using namespace censor;
using namespace ztst;

void censor_tests()
{
  describe("censor tests", []() -> void
  {
    describe("censor_raw_data", []() -> void
    {
      it("censors single word options", []()
      {
        std::string censored = censor_raw_data("hey --password hunter2 --innocent fine --passphrase 4 alright now more");
        Expect(censored).ToBe("hey --password **** --innocent fine --passphrase **** alright now more");
      });

      it("censors sensitive option values that include a space", []()
      {
        std::string censored = censor_raw_data("system do stuff --credentials \"good luck have fun\" positional.arg");
        Expect(censored).ToBe("system do stuff --credentials **** positional.arg");
      });

      it("censors sensitive options preceded by multiple spaces", []()
      {
        std::string censored = censor_raw_data("system do thangs --auth     mysecretpassword coolbean");
        Expect(censored).ToBe("system do thangs --auth     **** coolbean");
      });

      it("censors sensitive options in arg=value format", []()
      {
        std::string censored = censor_raw_data("system do thangs --auth=myauthiscool otherstuff");
        Expect(censored).ToBe("system do thangs --auth=**** otherstuff");
      });

      it("censors a sensitive option that has a single character option name", []()
      {
        std::string censored = censor_raw_data("command -p mysecretpassword --normal great");
        Expect(censored).ToBe("command -p **** --normal great");
      });

      it("censors options that include escaped quotes", []()
      {
        std::string censored = censor_raw_data("command -p \"bet you didn't \\\"think i could include a quote\" --normal socool");
        Expect(censored).ToBe("command -p **** --normal socool");
      });

      it("censors options that occur at the end of the string", []()
      {
        std::string censored = censor_raw_data("command one two three --tokenValue thisisattheend!");
        Expect(censored).ToBe("command one two three --tokenValue ****");
      });
    });
  });
}