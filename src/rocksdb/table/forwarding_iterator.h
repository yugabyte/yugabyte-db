#pragma once

#include "rocksdb/table/internal_iterator.h"
#include "rocksdb/table/iterator_wrapper.h"

namespace rocksdb {

class ForwardingIterator : public InternalIterator {
 public:
  explicit ForwardingIterator(InternalIterator* internal_iter, bool need_free_iter = true)
      : internal_iter_(internal_iter), need_free_iter_(need_free_iter) {}

  virtual ~ForwardingIterator() { internal_iter_.DeleteIter(!need_free_iter_); }

  virtual bool Valid() const override { return internal_iter_.Valid(); }

  virtual void Seek(const Slice& target) override { internal_iter_.Seek(target); }
  virtual void SeekToFirst() override { internal_iter_.SeekToFirst(); }
  virtual void SeekToLast() override { internal_iter_.SeekToLast(); }

  virtual void Next() override { internal_iter_.Next(); }
  virtual void Prev() override { internal_iter_.Prev(); }

  virtual Slice key() const override { return internal_iter_.key(); }
  virtual Slice value() const override { return internal_iter_.value(); }
  virtual Status status() const override { return internal_iter_.status(); }

  virtual Status PinData() override { return internal_iter_.PinData(); }
  virtual Status ReleasePinnedData() override { return internal_iter_.ReleasePinnedData(); }
  virtual bool IsKeyPinned() const override { return internal_iter_.IsKeyPinned(); }

 protected:
  IteratorWrapper internal_iter_;

 private:
  // No copying allowed
  ForwardingIterator(const ForwardingIterator&) = delete;
  ForwardingIterator& operator=(const ForwardingIterator&) = delete;
  bool need_free_iter_ = true;
};

}  // namespace rocksdb
