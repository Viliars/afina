#ifndef AFINA_STORAGE_SIMPLE_LRU_H
#define AFINA_STORAGE_SIMPLE_LRU_H

#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include <afina/Storage.h>

namespace Afina {
namespace Backend {

/**
 * # Map based implementation
 * That is NOT thread safe implementaiton!!
 */
class SimpleLRU : public Afina::Storage {
public:
    SimpleLRU(size_t max_size = 1024, size_t filter = 1024) : _max_size(max_size),
    _filter(filter)
    {
      _bits.resize(_filter, false);
    }

    ~SimpleLRU() {
        _lru_index.clear();
        _bits.clear();
        while(_lru_first != nullptr)
        {
          lru_node* buf = _lru_first;
          _lru_first = _lru_first->next;
          delete buf;
        }
    }

    // Implements Afina::Storage interface
    bool Put(const std::string &key, const std::string &value) override;

    // Implements Afina::Storage interface
    bool PutIfAbsent(const std::string &key, const std::string &value) override;

    // Implements Afina::Storage interface
    bool Set(const std::string &key, const std::string &value) override;

    // Implements Afina::Storage interface
    bool Delete(const std::string &key) override;

    // Implements Afina::Storage interface
    bool Get(const std::string &key, std::string &value) override;

private:
    bool _put(const std::string &key, const std::string &value);



    // LRU cache node
    using lru_node = struct lru_node {
        const std::string key;
        std::string value;
        lru_node* prev;
        lru_node* next;
    };

    void _up(lru_node& old_node);
    // Maximum number of bytes could be stored in this cache.
    // i.e all (keys+values) must be less the _max_size
    std::size_t _max_size;
    std::size_t _size = 0;
    std::size_t _filter;

    std::vector<bool> _bits;
    // Main storage of lru_nodes, elements in this list ordered descending by "freshness": in the head
    // element that wasn't used for longest time.
    //
    // List owns all nodes
    lru_node* _lru_first = nullptr;
    lru_node* _lru_last = nullptr;

    // Index of nodes from list above, allows fast random access to elements by lru_node#key
    std::map<std::reference_wrapper<const std::string>, std::reference_wrapper<lru_node>, std::less<std::string>> _lru_index;

    // for filter
    void _add(const std::string&);
    bool _possiblyContains(const std::string&);
    size_t _hash1(const std::string&);
    size_t _hash2(const std::string&);
    size_t _hash3(const std::string&);
};

} // namespace Backend
} // namespace Afina

#endif // AFINA_STORAGE_SIMPLE_LRU_H
