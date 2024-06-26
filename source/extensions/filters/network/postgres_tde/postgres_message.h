#pragma once

#include "source/common/buffer/buffer_impl.h"

#include "absl/strings/str_cat.h"
#include "absl/strings/str_join.h"
#include "fmt/printf.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace PostgresTDE {

/**
 * Postgres messages are described in official Postgres documentation:
 * https://www.postgresql.org/docs/current/protocol-message-formats.html
 *
 * Most of messages start with 1-byte message identifier followed by 4-bytes length field. Few
 * messages are defined without starting 1-byte character and are used during well-defined initial
 * stage of connection process.
 *
 * Messages are composed from various fields: 8, 16, 32-bit integers, String, Arrays, etc.
 *
 * Structures defined below have the same naming as types used in official Postgres documentation.
 *
 * Each structure has the following methods:
 * read - to read number of bytes from received buffer. The number of bytes depends on structure
 * type. toString - method returns displayable representation of the structure value.
 *
 */

// Interface to Postgres message class.
class Message {
public:
  enum ValidationResult { ValidationFailed, ValidationOK, ValidationNeedMoreData };

  virtual ~Message() = default;

  // read method should read only as many bytes from data
  // buffer as it is indicated in message's length field.
  // "length" parameter indicates how many bytes were indicated in Postgres message's
  // length field. "data" buffer may contain more bytes than "length".
  virtual bool read(const Buffer::Instance& data, const uint64_t length) PURE;

  virtual ValidationResult validate(const Buffer::Instance& data, const uint64_t,
                                    const uint64_t) PURE;

  // toString method provides displayable representation of
  // the Postgres message.
  virtual std::string toString() const PURE;

  virtual bool isWriteable() const PURE;
  virtual void write(Buffer::Instance&) const PURE;

protected:
  ValidationResult validation_result_{ValidationNeedMoreData};
};

// Template for integer types.
// Size of integer types is fixed and depends on the type of integer.
template <typename T> class Int {
public:
  Int() = default;
  Int(T value) : value_(value){};

  /**
   * Read integer value from data buffer.
   * @param data reference to a buffer containing data to read.
   * @param pos offset in the buffer where data to read is located. Successful read will advance
   * this parameter.
   * @param left number of bytes to be read to reach the end of Postgres message.
   * Successful read will adjust this parameter.
   * @return boolean value indicating whether read was successful. If read returns
   * false "pos" and "left" params are not updated. When read is not successful,
   * the caller should not continue reading next values from the data buffer
   * for the current message.
   */
  bool read(const Buffer::Instance& data, uint64_t& pos, uint64_t& left) {
    value_ = data.peekBEInt<T>(pos);
    pos += sizeof(T);
    left -= sizeof(T);
    return true;
  }

  Message::ValidationResult validate(const Buffer::Instance& data, const uint64_t, uint64_t& pos,
                                     uint64_t& left) {
    if (left < sizeof(T)) {
      return Message::ValidationFailed;
    }

    if ((data.length() - pos) < sizeof(T)) {
      return Message::ValidationNeedMoreData;
    }

    pos += sizeof(T);
    left -= sizeof(T);
    return Message::ValidationOK;
  }

  std::string toString() const { return fmt::format("[{}]", value_); }

  int32_t getSize() const { return sizeof(T); }

  void write(Buffer::Instance& to) const { to.writeBEInt(value_); };

  auto& value() { return value_; }

private:
  T value_{};
};

using Int32 = Int<uint32_t>;
using Int16 = Int<uint16_t>;
using Int8 = Int<uint8_t>;

// 8-bits character value.
using Byte1 = Int<char>;

// String type requires byte with zero value to indicate end of string.
class String {
public:
  String() = default;
  explicit String(std::string str) : value_(std::move(str)){};

  /**
   * See above for parameter and return value description.
   */
  bool read(const Buffer::Instance& data, uint64_t& pos, uint64_t& left);
  std::string toString() const;
  Message::ValidationResult validate(const Buffer::Instance&, const uint64_t start_offset,
                                     uint64_t&, uint64_t&);

  int32_t getSize() const { return value_.size() + 1; }

  void write(Buffer::Instance& to) const { to.add(value_.data(), value_.size() + 1); };

  auto& value() { return value_; }

private:
  // start_ and end_ are set by validate method.
  uint64_t start_;
  uint64_t end_;
  std::string value_;
};

// ByteN type is used as the last type in the Postgres message and contains
// sequence of bytes. The length must be deduced from message length.
class ByteN {
public:
  /**
   * See above for parameter and return value description.
   */
  bool read(const Buffer::Instance& data, uint64_t& pos, uint64_t& left);
  std::string toString() const;
  Message::ValidationResult validate(const Buffer::Instance&, const uint64_t, uint64_t&, uint64_t&);

  int32_t getSize() const { return value_.size(); }

  void write(Buffer::Instance& to) const { to.add(value_.data(), value_.size()); };

  auto& value() { return value_; }

private:
  std::vector<uint8_t> value_;
};

// VarByteN represents the structure consisting of 4 bytes of length
// indicating how many bytes follow.
// In Postgres documentation it is described as:
// - Int32
//   The number of bytes in the structure (this count does not include itself). Can be
//   zero. As a special case, -1 indicates a NULL (no result). No value bytes follow in the NULL
// case.
//
// - ByteN
// The sequence of bytes representing the value. Bytes are present only when length has a positive
// value.
class VarByteN {
public:
  VarByteN() = default;
  explicit VarByteN(std::vector<uint8_t> data) : value_(std::move(data)) {}

  /**
   * See above for parameter and return value description.
   */
  bool read(const Buffer::Instance& data, uint64_t& pos, uint64_t& left);
  std::string toString() const;
  Message::ValidationResult validate(const Buffer::Instance&, const uint64_t, uint64_t&, uint64_t&);

  int32_t getSize() const { return (value_.has_value() ? value_->size() : 0) + sizeof(int32_t); }

  void write(Buffer::Instance& to) const {
    if (value_.has_value()) {
      to.writeBEInt<int32_t>(value_->size());
      to.add(value_->data(), value_->size());
    } else {
      to.writeBEInt<int32_t>(-1);
    }
  }

  bool is_null() const { return !value_.has_value(); }

  auto& value() {
    ASSERT(value_.has_value());
    return *value_;
  }

  bool operator==(const VarByteN& other) const;
  bool operator!=(const VarByteN& other) const;

private:
  std::optional<std::vector<uint8_t>> value_;
};

// Array contains one or more values of the same type.
template <typename T> class Array {
public:
  Array() = default;
  explicit Array(std::vector<std::unique_ptr<T>> elements) : value_(std::move(elements)) {}

  /**
   * See above for parameter and return value description.
   */
  bool read(const Buffer::Instance& data, uint64_t& pos, uint64_t& left) {
    uint16_t size = data.peekBEInt<uint16_t>(pos);
    pos += sizeof(uint16_t);
    left -= sizeof(uint16_t);
    for (uint16_t i = 0; i < size; i++) {
      value_[i]->read(data, pos, left);
    }
    return true;
  }

  std::string toString() const {
    std::string out = fmt::format("[Array of {}:{{", value_.size());

    // Iterate through all elements in the array.
    // No delimiter is required between elements, as each
    // element is wrapped in "[]" or "{}".
    for (const auto& i : value_) {
      absl::StrAppend(&out, i->toString());
    }
    absl::StrAppend(&out, "}]");

    return out;
  }
  Message::ValidationResult validate(const Buffer::Instance& data, const uint64_t start_offset,
                                     uint64_t& pos, uint64_t& left) {
    // First read the 16 bits value which indicates how many
    // elements there are in the array.
    if (left < sizeof(uint16_t)) {
      return Message::ValidationFailed;
    }

    if ((data.length() - pos) < sizeof(uint16_t)) {
      return Message::ValidationNeedMoreData;
    }

    uint16_t size = data.peekBEInt<uint16_t>(pos);
    uint64_t orig_pos = pos;
    uint64_t orig_left = left;
    pos += sizeof(uint16_t);
    left -= sizeof(uint16_t);
    if (size != 0) {
      for (uint16_t i = 0; i < size; i++) {
        auto item = std::make_unique<T>();
        Message::ValidationResult result = item->validate(data, start_offset, pos, left);
        if (Message::ValidationOK != result) {
          pos = orig_pos;
          left = orig_left;
          value_.clear();
          return result;
        }
        value_.push_back(std::move(item));
      }
    }
    return Message::ValidationOK;
  }

  int32_t getSize() const {
    uint16_t size = 0;
    for (auto& elem : value_) {
      size += elem->getSize();
    }
    return sizeof(uint16_t) + size;
  }

  void write(Buffer::Instance& to) const {
    to.writeBEInt<uint16_t>(value_.size());
    for (const std::unique_ptr<T>& elem : value_) {
      elem->write(to);
    }
  }

  auto& value() { return value_; }

private:
  std::vector<std::unique_ptr<T>> value_;
};

// Repeated means reading the value of the same type while it's possible
template <typename T> class Repeated {
public:
  Repeated() = default;
  explicit Repeated(std::vector<std::unique_ptr<T>> elements) : value_(std::move(elements)) {}

  /**
   * See above for parameter and return value description.
   */
  bool read(const Buffer::Instance& data, uint64_t& pos, uint64_t& left) {
    for (size_t i = 0; i < value_.size(); i++) {
      if (!value_[i]->read(data, pos, left)) {
        return false;
      }
    }
    return true;
  }

  std::string toString() const {
    std::string out;

    // Iterate through all repeated elements.
    // No delimiter is required between elements, as each
    // element is wrapped in "[]" or "{}".
    for (const auto& i : value_) {
      absl::StrAppend(&out, i->toString());
    }
    return out;
  }
  Message::ValidationResult validate(const Buffer::Instance& data, const uint64_t start_offset,
                                     uint64_t& pos, uint64_t& left) {
    if ((data.length() - pos) < left) {
      return Message::ValidationNeedMoreData;
    }

    while (left != 0) {
      auto item = std::make_unique<T>();
      Message::ValidationResult result = item->validate(data, start_offset, pos, left);
      if (result != Message::ValidationOK) {
        break;
      }
      value_.push_back(std::move(item));
    }

    return Message::ValidationOK;
  }

  int32_t getSize() const {
    uint16_t size = 0;
    for (auto& elem : value_) {
      size += elem->getSize();
    }
    return size;
  }

  void write(Buffer::Instance& to) const {
    for (const std::unique_ptr<T>& elem : value_) {
      elem->write(to);
    }
  };

  auto& value() { return value_; }

private:
  std::vector<std::unique_ptr<T>> value_;
};

// Sequence is tuple like structure, which binds together
// set of several fields of different types.
template <typename... Types> class Sequence;

template <typename FirstField, typename... Remaining> class Sequence<FirstField, Remaining...> {
  FirstField first_;
  Sequence<Remaining...> remaining_;

public:
  Sequence() = default;
  explicit Sequence(FirstField first, Remaining... remaining)
      : first_(std::move(first)), remaining_(std::move(remaining)...) {}

  std::string toString() const { return absl::StrCat(first_.toString(), remaining_.toString()); }

  /**
   * Implementation of "read" method for variadic template.
   * It reads data for the current type and invokes read operation
   * for remaining types.
   * See above for parameter and return value description for individual types.
   */
  bool read(const Buffer::Instance& data, uint64_t& pos, uint64_t& left) {
    bool result = first_.read(data, pos, left);
    if (!result) {
      return false;
    }
    return remaining_.read(data, pos, left);
  }

  Message::ValidationResult validate(const Buffer::Instance& data, const uint64_t start_offset,
                                     uint64_t& pos, uint64_t& left) {
    uint64_t orig_pos = pos;
    uint64_t orig_left = left;
    Message::ValidationResult result = first_.validate(data, start_offset, pos, left);
    if (result != Message::ValidationOK) {
      pos = orig_pos;
      left = orig_left;
      return result;
    }

    result = remaining_.validate(data, start_offset, pos, left);
    if (result != Message::ValidationOK) {
      pos = orig_pos;
      left = orig_left;
      return result;
    }

    return Message::ValidationOK;
  }

  int32_t getSize() const { return first_.getSize() + remaining_.getSize(); }

  void write(Buffer::Instance& to) const {
    first_.write(to);
    remaining_.write(to);
  };

  template<int Index>
  constexpr auto& value() {
    if constexpr (Index == 0) {
      return first_;
    }
    else {
      return remaining_.template value<Index - 1>();
    }
  }
};

// Terminal template definition for variadic Sequence template.
template <> class Sequence<> {
public:
  Sequence<>() = default;
  std::string toString() const { return ""; }
  bool read(const Buffer::Instance&, uint64_t&, uint64_t&) { return true; }
  Message::ValidationResult validate(const Buffer::Instance&, const uint64_t, uint64_t&,
                                     uint64_t&) {
    return Message::ValidationOK;
  }

  int32_t getSize() const { return 0; }
  void write(Buffer::Instance&) const {};
};

template <typename... Types> class MessageImpl : public Message, public Sequence<Types...> {
public:
  MessageImpl() = default;
  explicit MessageImpl(Types... fields) : Sequence<Types...>(std::move(fields)...) {}

  ~MessageImpl() override = default;

  bool read(const Buffer::Instance& data, const uint64_t length) override {
    // Do not call read unless validation was successful.
    ASSERT(validation_result_ == ValidationOK);
    uint64_t pos = 0;
    uint64_t left = length;
    return Sequence<Types...>::read(data, pos, left);
  }

  Message::ValidationResult validate(const Buffer::Instance& data, const uint64_t start_pos,
                                     const uint64_t length) override {
    uint64_t pos = start_pos;
    uint64_t left = length;
    validation_result_ = Sequence<Types...>::validate(data, start_pos, pos, left);
    return validation_result_;
  }

  std::string toString() const override { return Sequence<Types...>::toString(); }

  using Message::write;
  void write(Buffer::Instance& to, char identifier) const {
    to.writeByte(identifier);
    to.writeBEInt<uint32_t>(Sequence<Types...>::getSize() + sizeof(uint32_t));
    Sequence<Types...>::write(to);
  }

  bool isWriteable() const override { return false; }
  void write(Buffer::Instance&) const override {
    // Messages without identifier are validate-only and should not be written
    ASSERT(false);
  }

private:
  // Message::ValidationResult validation_result_;
};

template <> class MessageImpl<> : public Message {
public:
  ~MessageImpl() override = default;

  bool read(const Buffer::Instance&, const uint64_t) override { return true; }

  Message::ValidationResult validate(const Buffer::Instance&, const uint64_t,
                                     const uint64_t) override {
    return ValidationOK;
  }

  std::string toString() const override { return ""; }

  using Message::write;
  void write(Buffer::Instance& to, char identifier) const {
    to.writeByte(identifier);
    to.writeBEInt<uint32_t>(sizeof(uint32_t));
  }

  bool isWriteable() const override { return false; }
  void write(Buffer::Instance&) const override {
    // Messages without identifier are validate-only and should not be written
    ASSERT(false);
  }
};

template <char Identifier, typename... Types> class TypedMessage : public MessageImpl<Types...> {
public:
  // Inherit constructor
  using MessageImpl<Types...>::MessageImpl;

  bool isWriteable() const override { return true; }
  void write(Buffer::Instance& to) const override { MessageImpl<Types...>::write(to, Identifier); }
};

template <char Identifier>
class TypedMessage<Identifier> : public MessageImpl<> {
public:
  // Inherit constructor
  using MessageImpl<>::MessageImpl;

  bool isWriteable() const override { return true; }
  void write(Buffer::Instance& to) const override { MessageImpl<>::write(to, Identifier); }
};

// Helper function to create pointer to a Sequence structure and is used by Postgres
// decoder after learning the type of Postgres message.
template <typename... Types> std::unique_ptr<Message> createMsgBodyReader() {
  return std::make_unique<MessageImpl<Types...>>();
}

using MessagePtr = std::unique_ptr<Message>;

} // namespace PostgresTDE
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
