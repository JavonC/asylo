/*
 *
 * Copyright 2017 Asylo authors
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 */

#ifndef ASYLO_UTIL_STATUSOR_H_
#define ASYLO_UTIL_STATUSOR_H_

#include "asylo/util/logging.h"
#include "asylo/util/status.h"
#include "asylo/util/status_error_space.h"

namespace asylo {

/// A class for representing either a usable value, or an error.
///
/// A StatusOr object either contains a value of type `T` or a Status object
/// explaining why such a value is not present. The type `T` must be
/// copy-constructible and/or move-constructible.
///
/// The state of a StatusOr object may be determined by calling ok() or
/// status(). The ok() method returns true if the object contains a valid value.
/// The status() method returns the internal Status object. A StatusOr object
/// that contains a valid value will return an OK Status for a call to status().
///
/// A value of type `T` may be extracted from a StatusOr object through a call
/// to ValueOrDie(). This function should only be called if a call to ok()
/// returns true. Sample usage:
///
/// ```
///   asylo::StatusOr<Foo> result = CalculateFoo();
///   if (result.ok()) {
///     Foo foo = result.ValueOrDie();
///     foo->DoSomethingCool();
///   } else {
///     LOG(ERROR) << result.status();
///  }
/// ```
///
/// If `T` is a move-only type, like `std::unique_ptr<>`, then the value should
/// only be extracted after invoking `std::move()` on the StatusOr object.
/// Sample usage:
///
/// ```
///   asylo::StatusOr<std::unique_ptr<Foo>> = CalculateFoo();
///   if (result.ok()) {
///     std::unique_ptr<Foo> foo = std::move(result).ValueOrDie();
///     foo->DoSomethingCool();
///   } else {
///     LOG(ERROR) << result.status();
///   }
/// ```
///
/// StatusOr is provided for the convenience of implementing functions that
/// return some value but may fail during execution. For instance, consider a
/// function with the following signature:
///
/// ```
///   asylo::Status CalculateFoo(int *output);
/// ```
///
/// This function may instead be written as:
///
/// ```
///   asylo::StatusOr<int> CalculateFoo();
/// ```
template <class T>
class StatusOr {
 public:
  /// Constructs a StatusOr object that contains a non-OK status.
  /// The non-OK status has an error code of -1. This is a non-standard POSIX
  /// error code and is used in this context to indicate an unknown error.
  ///
  /// This constructor is marked `explicit` to prevent attempts to `return {}`
  /// from a function with a return type of, for example,
  /// `StatusOr<std::vector<int>>`. While `return {}` seems like it would return
  /// an empty vector, it will actually invoke the default constructor of
  /// StatusOr.
  explicit StatusOr()
      : variant_(Status(error::GoogleError::UNKNOWN, "Unknown error")),
        has_value_(false) {}

  ~StatusOr() {
    if (has_value_) {
      variant_.value_.~T();
    } else {
      variant_.status_.~Status();
    }
  }

  /// Constructs a StatusOr object with the given non-OK Status object. All
  /// calls to ValueOrDie() on this object will abort. The given `status` must
  /// not be an OK status, otherwise this constructor will abort.
  ///
  /// This constructor is not declared explicit so that a function with a return
  /// type of `StatusOr<T>` can return a Status object, and the status will be
  /// implicitly converted to the appropriate return type as a matter of
  /// convenience.
  ///
  /// \param status The non-OK Status object to initalize to.
  StatusOr(const Status &status) : variant_(status), has_value_(false) {
    if (status.ok()) {
      LOG(FATAL) << "Cannot instantiate StatusOr with Status::OkStatus()";
    }
  }

  /// Constructs a StatusOr object containing `value`. The resulting object is
  /// considered to have an OK status. The wrapped element can be accessed with
  /// ValueOrDie().
  ///
  /// This constructor is not declared `explicit` so that a function with a
  /// return type of `StatusOr<T>` can return an element of type `T`, and this
  /// value will be implicitly converted to the appropriate return type as
  /// matter of convenience.
  ///
  /// \param value The wrapped value to initialize to.
  StatusOr(const T &value)
      : variant_(value), has_value_(true) {}

  /// Constructs a StatusOr object that contains `value`. The resulting object
  /// is considered to have an OK status. The wrapped element can be accessed
  /// with ValueOrDie().
  ///
  /// This constructor is not declared explicit so that a function with a return
  /// type of `StatusOr<T>` can return an element of type `T &&`, and this value
  /// will be implicitly converted to the appropriate return type as a matter of
  /// convenience.
  ///
  /// \param value The value to move-initialize to.
  StatusOr(T &&value)
      : variant_(std::move(value)), has_value_(true) {}

  /// Copy constructor.
  StatusOr(const StatusOr &other) : has_value_(other.has_value_) {
    if (has_value_) {
      new (&variant_) variant(other.variant_.value_);
    } else {
      new (&variant_) variant(other.variant_.status_);
    }
  }

  /// Copy-assignment operator.
  ///
  /// \param other The StatusOr object to copy.
  StatusOr &operator=(const StatusOr &other) {
    // Check for self-assignment.
    if (this == &other) {
      return *this;
    }

    // Construct the variant object using the variant object of the source.
    if (other.has_value_) {
      AssignValue(other.variant_.value_);
    } else {
      AssignStatus(other.variant_.status_);
    }
    return *this;
  }

  /// Move constructor.
  ///
  /// Sets `other` to contain a non-OK status with a `StatusError::INVALID`
  /// error code.
  ///
  /// \param other The StatusOr object to copy and set to a non-OK status.
  StatusOr(StatusOr &&other) : has_value_(other.has_value_) {
    if (has_value_) {
      new (&variant_) variant(std::move(other.variant_.value_));
    } else {
      new (&variant_) variant(std::move(other.variant_.status_));
    }

    // The donor object may have previously held a valid value that is now
    // inaccessible. Clear its current state.
    other.Clear();
  }

  /// Move-assignment operator.
  ///
  /// Sets `other` to contain a non-OK status with a `StatusError::INVALID`
  /// error code.
  ///
  /// \param other The StatusOr object to assign from and set to a non-OK
  /// status.
  StatusOr &operator=(StatusOr &&other) {
    // Check for self-assignment.
    if (this == &other) {
      return *this;
    }

    // Construct the variant object using the variant object of the donor.
    if (other.has_value_) {
      AssignValue(std::move(other.variant_.value_));
    } else {
      AssignStatus(std::move(other.variant_.status_));
    }

    // The donor object may have previously held a valid value that is now
    // inaccessible. Clear its current state.
    other.Clear();

    return *this;
  }


  /// Indicates whether the object contains a `T` value.
  ///
  /// \return True if this StatusOr object's status is OK (i.e. a call to ok()
  /// returns true). If this function returns true, then it is safe to access
  /// the wrapped element through a call to ValueOrDie().
  bool ok() const {
    return has_value_;
  }

  /// Gets the stored status object, or an OK status if a `T` value is stored.
  ///
  /// \return The stored non-OK status object, or an OK status if this object
  ///         has a value.
  Status status() const {
    return ok() ? Status::OkStatus() : variant_.status_;
  }

  /// Gets the stored `T` value.
  ///
  /// This method should only be called if this StatusOr object's status is OK
  /// (i.e. a call to ok() returns true), otherwise this call will abort.
  ///
  /// \return The stored `T` value.
  const T &ValueOrDie() const & {
    if (!ok()) {
      LOG(FATAL) << "Object does not have a usable value";
    }
    return variant_.value_;
  }

  /// Gets a mutable reference to the stored `T` value.
  ///
  /// This method should only be called if this StatusOr object's status is OK
  /// (i.e. a call to ok() returns true), otherwise this call will abort.
  ///
  /// \return The stored `T` value.
  T &ValueOrDie() & {
    if (!ok()) {
      LOG(FATAL) << "Object does not have a usable value";
    }
    return variant_.value_;
  }

  /// Moves and returns the internally-stored `T` value.
  ///
  /// This method should only be called if this StatusOr object's status is OK
  /// (i.e. a call to ok() returns true), otherwise this call will abort. The
  /// StatusOr object is invalidated after this call and will be updated to
  /// contain a non-OK status with a `StatusError::INVALID` error code.
  ///
  /// \return The stored `T` value.
  T ValueOrDie() && {
    if (!ok()) {
      LOG(FATAL) << "Object does not have a usable value";
    }
    T tmp(std::move(variant_.value_));

    // Invalidate this StatusOr object.
    Clear();
    return std::move(tmp);
  }

 private:
  // Clears the current state of the StatusOr object and sets it to contain a
  // Status object with a StatusError::INVALID error code.
  void Clear() {
    AssignStatus(Status(error::StatusError::INVALID, "The object was moved"));
  }

  // Resets the |variant_| member to contain the |status| and sets |has_value_|
  // to indicate that the StatusOr object does not have a value. Destroys the
  // existing |variant_| member if necessary.
  template <class U>
  void AssignStatus(U &&status) {
    if (ok()) {
      variant_.value_.~T();
      new (&variant_) variant(std::forward<U>(status));
    } else {
      // Reuse the existing Status object.
      variant_.status_ = std::forward<U>(status);
    }
    has_value_ = false;
  }

  // Resets the |variant_| member to contain the |value| and sets |has_value_|
  // to indicate that the StatusOr object has a value. Destroys the existing
  // |variant_| member.
  template <class U>
  void AssignValue(U &&value) {
    if (ok()) {
      // We cannot assume that T is move-assignable.
      variant_.value_.~T();
    } else {
      variant_.status_.~Status();
    }
    new (&variant_) variant(std::forward<U>(value));
    has_value_ = true;
  }

  union variant {
    // A non-OK status.
    Status status_;

    // An element of type T.
    T value_;

    variant() {}

    variant(const Status &status) : status_(status) {}

    variant(Status &&status) : status_(std::move(status)) {}

    variant(const T &value) : value_(value) {}

    variant(T &&value) : value_(std::move(value)) {}

    // This destructor must be explicitly defined because it is deleted due to
    // the variant type having non-static data members with non-trivial
    // destructors.
    ~variant() {}
  };

  // One of: a non-OK status or an element of type T.
  variant variant_;

  // Indicates the active member of the variant_ member.
  //
  // A value of true indicates that value_ is the active member of variant_.
  //
  // A value of false indicates that status_ is the active member of variant_.
  bool has_value_;
};

}  // namespace asylo

#endif  // ASYLO_UTIL_STATUSOR_H_
