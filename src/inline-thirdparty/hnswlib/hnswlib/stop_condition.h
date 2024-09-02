#pragma once
#include "space_l2.h"
#include "space_ip.h"
#include <assert.h>
#include <unordered_map>

namespace hnswlib {

template<typename DOCIDTYPE>
class BaseMultiVectorSpace : public SpaceInterface<float> {
 public:
    virtual DOCIDTYPE get_doc_id(const void *datapoint) = 0;

    virtual void set_doc_id(void *datapoint, DOCIDTYPE doc_id) = 0;
};


template<typename DOCIDTYPE>
class MultiVectorL2Space : public BaseMultiVectorSpace<DOCIDTYPE> {
    DISTFUNC<float> fstdistfunc_;
    size_t data_size_;
    size_t vector_size_;
    size_t dim_;

 public:
    MultiVectorL2Space(size_t dim) {
        fstdistfunc_ = L2Sqr;
#if defined(USE_SSE) || defined(USE_AVX) || defined(USE_AVX512)
    #if defined(USE_AVX512)
        if (AVX512Capable())
            L2SqrSIMD16Ext = L2SqrSIMD16ExtAVX512;
        else if (AVXCapable())
            L2SqrSIMD16Ext = L2SqrSIMD16ExtAVX;
    #elif defined(USE_AVX)
        if (AVXCapable())
            L2SqrSIMD16Ext = L2SqrSIMD16ExtAVX;
    #endif

        if (dim % 16 == 0)
            fstdistfunc_ = L2SqrSIMD16Ext;
        else if (dim % 4 == 0)
            fstdistfunc_ = L2SqrSIMD4Ext;
        else if (dim > 16)
            fstdistfunc_ = L2SqrSIMD16ExtResiduals;
        else if (dim > 4)
            fstdistfunc_ = L2SqrSIMD4ExtResiduals;
#endif
        dim_ = dim;
        vector_size_ = dim * sizeof(float);
        data_size_ = vector_size_ + sizeof(DOCIDTYPE);
    }

    size_t get_data_size() override {
        return data_size_;
    }

    DISTFUNC<float> get_dist_func() override {
        return fstdistfunc_;
    }

    void *get_dist_func_param() override {
        return &dim_;
    }

    DOCIDTYPE get_doc_id(const void *datapoint) override {
        return *(DOCIDTYPE *)((char *)datapoint + vector_size_);
    }

    void set_doc_id(void *datapoint, DOCIDTYPE doc_id) override {
        *(DOCIDTYPE*)((char *)datapoint + vector_size_) = doc_id;
    }

    ~MultiVectorL2Space() {}
};


template<typename DOCIDTYPE>
class MultiVectorInnerProductSpace : public BaseMultiVectorSpace<DOCIDTYPE> {
    DISTFUNC<float> fstdistfunc_;
    size_t data_size_;
    size_t vector_size_;
    size_t dim_;

 public:
    MultiVectorInnerProductSpace(size_t dim) {
        fstdistfunc_ = InnerProductDistance;
#if defined(USE_AVX) || defined(USE_SSE) || defined(USE_AVX512)
    #if defined(USE_AVX512)
        if (AVX512Capable()) {
            InnerProductSIMD16Ext = InnerProductSIMD16ExtAVX512;
            InnerProductDistanceSIMD16Ext = InnerProductDistanceSIMD16ExtAVX512;
        } else if (AVXCapable()) {
            InnerProductSIMD16Ext = InnerProductSIMD16ExtAVX;
            InnerProductDistanceSIMD16Ext = InnerProductDistanceSIMD16ExtAVX;
        }
    #elif defined(USE_AVX)
        if (AVXCapable()) {
            InnerProductSIMD16Ext = InnerProductSIMD16ExtAVX;
            InnerProductDistanceSIMD16Ext = InnerProductDistanceSIMD16ExtAVX;
        }
    #endif
    #if defined(USE_AVX)
        if (AVXCapable()) {
            InnerProductSIMD4Ext = InnerProductSIMD4ExtAVX;
            InnerProductDistanceSIMD4Ext = InnerProductDistanceSIMD4ExtAVX;
        }
    #endif

        if (dim % 16 == 0)
            fstdistfunc_ = InnerProductDistanceSIMD16Ext;
        else if (dim % 4 == 0)
            fstdistfunc_ = InnerProductDistanceSIMD4Ext;
        else if (dim > 16)
            fstdistfunc_ = InnerProductDistanceSIMD16ExtResiduals;
        else if (dim > 4)
            fstdistfunc_ = InnerProductDistanceSIMD4ExtResiduals;
#endif
        vector_size_ = dim * sizeof(float);
        data_size_ = vector_size_ + sizeof(DOCIDTYPE);
    }

    size_t get_data_size() override {
        return data_size_;
    }

    DISTFUNC<float> get_dist_func() override {
        return fstdistfunc_;
    }

    void *get_dist_func_param() override {
        return &dim_;
    }

    DOCIDTYPE get_doc_id(const void *datapoint) override {
        return *(DOCIDTYPE *)((char *)datapoint + vector_size_);
    }

    void set_doc_id(void *datapoint, DOCIDTYPE doc_id) override {
        *(DOCIDTYPE*)((char *)datapoint + vector_size_) = doc_id;
    }

    ~MultiVectorInnerProductSpace() {}
};


template<typename DOCIDTYPE, typename dist_t>
class MultiVectorSearchStopCondition : public BaseSearchStopCondition<dist_t> {
    size_t curr_num_docs_;
    size_t num_docs_to_search_;
    size_t ef_collection_;
    std::unordered_map<DOCIDTYPE, size_t> doc_counter_;
    std::priority_queue<std::pair<dist_t, DOCIDTYPE>> search_results_;
    BaseMultiVectorSpace<DOCIDTYPE>& space_;

 public:
    MultiVectorSearchStopCondition(
        BaseMultiVectorSpace<DOCIDTYPE>& space,
        size_t num_docs_to_search,
        size_t ef_collection = 10)
        : space_(space) {
            curr_num_docs_ = 0;
            num_docs_to_search_ = num_docs_to_search;
            ef_collection_ = std::max(ef_collection, num_docs_to_search);
        }

    void add_point_to_result(labeltype label, const void *datapoint, dist_t dist) override {
        DOCIDTYPE doc_id = space_.get_doc_id(datapoint);
        if (doc_counter_[doc_id] == 0) {
            curr_num_docs_ += 1;
        }
        search_results_.emplace(dist, doc_id);
        doc_counter_[doc_id] += 1;
    }

    void remove_point_from_result(labeltype label, const void *datapoint, dist_t dist) override {
        DOCIDTYPE doc_id = space_.get_doc_id(datapoint);
        doc_counter_[doc_id] -= 1;
        if (doc_counter_[doc_id] == 0) {
            curr_num_docs_ -= 1;
        }
        search_results_.pop();
    }

    bool should_stop_search(dist_t candidate_dist, dist_t lowerBound) override {
        bool stop_search = candidate_dist > lowerBound && curr_num_docs_ == ef_collection_;
        return stop_search;
    }

    bool should_consider_candidate(dist_t candidate_dist, dist_t lowerBound) override {
        bool flag_consider_candidate = curr_num_docs_ < ef_collection_ || lowerBound > candidate_dist;
        return flag_consider_candidate;
    }

    bool should_remove_extra() override {
        bool flag_remove_extra = curr_num_docs_ > ef_collection_;
        return flag_remove_extra;
    }

    void filter_results(std::vector<std::pair<dist_t, labeltype >> &candidates) override {
        while (curr_num_docs_ > num_docs_to_search_) {
            dist_t dist_cand = candidates.back().first;
            dist_t dist_res = search_results_.top().first;
            assert(dist_cand == dist_res);
            DOCIDTYPE doc_id = search_results_.top().second;
            doc_counter_[doc_id] -= 1;
            if (doc_counter_[doc_id] == 0) {
                curr_num_docs_ -= 1;
            }
            search_results_.pop();
            candidates.pop_back();
        }
    }

    ~MultiVectorSearchStopCondition() {}
};


template<typename dist_t>
class EpsilonSearchStopCondition : public BaseSearchStopCondition<dist_t> {
    float epsilon_;
    size_t min_num_candidates_;
    size_t max_num_candidates_;
    size_t curr_num_items_;

 public:
    EpsilonSearchStopCondition(float epsilon, size_t min_num_candidates, size_t max_num_candidates) {
        assert(min_num_candidates <= max_num_candidates);
        epsilon_ = epsilon;
        min_num_candidates_ = min_num_candidates;
        max_num_candidates_ = max_num_candidates;
        curr_num_items_ = 0;
    }

    void add_point_to_result(labeltype label, const void *datapoint, dist_t dist) override {
        curr_num_items_ += 1;
    }

    void remove_point_from_result(labeltype label, const void *datapoint, dist_t dist) override {
        curr_num_items_ -= 1;
    }

    bool should_stop_search(dist_t candidate_dist, dist_t lowerBound) override {
        if (candidate_dist > lowerBound && curr_num_items_ == max_num_candidates_) {
            // new candidate can't improve found results
            return true;
        }
        if (candidate_dist > epsilon_ && curr_num_items_ >= min_num_candidates_) {
            // new candidate is out of epsilon region and
            // minimum number of candidates is checked
            return true;
        }
        return false;
    }

    bool should_consider_candidate(dist_t candidate_dist, dist_t lowerBound) override {
        bool flag_consider_candidate = curr_num_items_ < max_num_candidates_ || lowerBound > candidate_dist;
        return flag_consider_candidate;
    }

    bool should_remove_extra() {
        bool flag_remove_extra = curr_num_items_ > max_num_candidates_;
        return flag_remove_extra;
    }

    void filter_results(std::vector<std::pair<dist_t, labeltype >> &candidates) override {
        while (!candidates.empty() && candidates.back().first > epsilon_) {
            candidates.pop_back();
        }
        while (candidates.size() > max_num_candidates_) {
            candidates.pop_back();
        }
    }

    ~EpsilonSearchStopCondition() {}
};
}  // namespace hnswlib
