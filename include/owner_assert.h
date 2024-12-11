//
// Created by 张凯文 on 2024/3/26.
//

#ifndef PPTPN_INCLUDE_OWNER_ASSERT_H
#define PPTPN_INCLUDE_OWNER_ASSERT_H

#include <boost/assert.hpp>

#define ASSERT_TDG_RAP_NODE_ID(condition, message) \
  do { \
    if (!(condition)) {\
      auto assert_func = [&]() {};                 \
      assert_func();                               \
      BOOST_ASSERT_MSG(condition, message);        \
    } \
  } while (0)

#endif //PPTPN_INCLUDE_OWNER_ASSERT_H
