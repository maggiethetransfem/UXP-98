/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/* Cast operations to supplement the built-in casting operations. */

#ifndef mozilla_Casting_h
#define mozilla_Casting_h

#include "mozilla/Assertions.h"
#include "mozilla/TypeTraits.h"

#include <limits>
#include <cmath>
#include <type_traits>

namespace mozilla {

/**
 * Sets the outparam value of type |To| with the same underlying bit pattern of
 * |aFrom|.
 *
 * |To| and |From| must be types of the same size; be careful of cross-platform
 * size differences, or this might fail to compile on some but not all
 * platforms.
 *
 * There is also a variant that returns the value directly.  In most cases, the
 * two variants should be identical.  However, in the specific case of x86
 * chips, the behavior differs: returning floating-point values directly is done
 * through the x87 stack, and x87 loads and stores turn signaling NaNs into
 * quiet NaNs... silently.  Returning floating-point values via outparam,
 * however, is done entirely within the SSE registers when SSE2 floating-point
 * is enabled in the compiler, which has semantics-preserving behavior you would
 * expect.
 *
 * If preserving the distinction between signaling NaNs and quiet NaNs is
 * important to you, you should use the outparam version.  In all other cases,
 * you should use the direct return version.
 */
template<typename To, typename From>
inline void
BitwiseCast(const From aFrom, To* aResult)
{
  static_assert(sizeof(From) == sizeof(To),
                "To and From must have the same size");
  union
  {
    From mFrom;
    To mTo;
  } u;
  u.mFrom = aFrom;
  *aResult = u.mTo;
}

template<typename To, typename From>
inline To
BitwiseCast(const From aFrom)
{
  To temp;
  BitwiseCast<To, From>(aFrom, &temp);
  return temp;
}

namespace detail {

template <typename In, typename Out>
bool IsInBounds(In aIn)
{
  const long double value = static_cast<long double>(aIn);
  const long double outMin =
    static_cast<long double>(std::numeric_limits<Out>::lowest());
  const long double outMax =
    static_cast<long double>(std::numeric_limits<Out>::max());

  if (value < outMin || value > outMax) {
    return false;
  }

  if (!std::is_floating_point<In>::value &&
      std::is_floating_point<Out>::value) {
    const long double safeInteger =
      ldexpl(1.0L, std::numeric_limits<Out>::digits);
    if (std::is_signed<In>::value) {
      if (value < -safeInteger || value > safeInteger) {
        return false;
      }
    } else if (value >= safeInteger) {
      return false;
    }
  }

  return true;
}

} // namespace detail

/**
 * Cast a value of type |From| to a value of type |To|, asserting that the cast
 * will be a safe cast per C++ (that is, that |to| is in the range of values
 * permitted for the type |From|).
 * In particular, this will fail if a integer cann
 */
template<typename To, typename From>
inline To
AssertedCast(const From aFrom)
{
  static_assert(std::is_arithmetic<To>::value &&
                std::is_arithmetic<From>::value,
                "AssertedCast requires arithmetic types");
  MOZ_ASSERT((detail::IsInBounds<From, To>(aFrom)));
  return static_cast<To>(aFrom);
}

/**
 * Cast a value of numeric type |From| to a value of numeric type |To|, release
 * asserting that the cast will be a safe cast per C++ (that is, that |to| is in
 * the range of values permitted for the type |From|).
 * In particular, this will fail if a integer cannot be represented exactly as a
 * floating point value, because it's too large.
 */
template<typename To, typename From>
inline To
ReleaseAssertedCast(const From aFrom)
{
  static_assert(std::is_arithmetic<To>::value &&
                std::is_arithmetic<From>::value,
                "ReleaseAssertedCast requires arithmetic types");
  MOZ_RELEASE_ASSERT((detail::IsInBounds<From, To>(aFrom)));
  return static_cast<To>(aFrom);
}

} // namespace mozilla

#endif /* mozilla_Casting_h */
