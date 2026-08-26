// fork-local webrtc stub header. NOT real webrtc. Provides just enough of
// webrtc::scoped_refptr for the g3 fake-platform webrtc glue to compile when
// webrtc is disabled (the only configuration this fork builds). Never vendor
// or build real webrtc.
#ifndef THIRD_PARTY_WEBRTC_STUB_API_SCOPED_REFPTR_H_
#define THIRD_PARTY_WEBRTC_STUB_API_SCOPED_REFPTR_H_

#include <cstddef>
#include <utility>

namespace webrtc {

template <typename T>
class scoped_refptr {
 public:
  scoped_refptr() = default;
  scoped_refptr(std::nullptr_t) {}                      // NOLINT
  scoped_refptr(T* ptr) : ptr_(ptr) {}                  // NOLINT
  scoped_refptr(const scoped_refptr&) = default;
  scoped_refptr(scoped_refptr&& other) noexcept
      : ptr_(std::exchange(other.ptr_, nullptr)) {}
  scoped_refptr& operator=(const scoped_refptr&) = default;
  scoped_refptr& operator=(scoped_refptr&& other) noexcept {
    ptr_ = std::exchange(other.ptr_, nullptr);
    return *this;
  }

  T* get() const { return ptr_; }
  T* operator->() const { return ptr_; }
  T& operator*() const { return *ptr_; }
  explicit operator bool() const { return ptr_ != nullptr; }

  friend bool operator==(const scoped_refptr& a, std::nullptr_t) {
    return a.ptr_ == nullptr;
  }
  friend bool operator!=(const scoped_refptr& a, std::nullptr_t) {
    return a.ptr_ != nullptr;
  }

 private:
  T* ptr_ = nullptr;
};

}  // namespace webrtc

#endif  // THIRD_PARTY_WEBRTC_STUB_API_SCOPED_REFPTR_H_
