#include "SimpleLRU.h"


namespace Afina {
namespace Backend {

bool SimpleLRU::_put(const std::string &key, const std::string &value)
{
  std::size_t size_of_new_elem = key.size() + value.size();
  if(size_of_new_elem > _max_size) return false;
  // TODO Если нет места, то надо удалить ненужные элементы

  while (size_of_new_elem + _size > _max_size)
  {
    lru_node* buf = _lru_first;
    _lru_index.erase(buf->key);
    _lru_first = _lru_first->next;
    _lru_first->prev = nullptr;
    _size -= (buf->value.size() + buf->key.size());
    delete buf;
  }

  lru_node* buf = new lru_node{key, value};
  if(_lru_first != nullptr)
  {
    buf->prev = _lru_last;
    _lru_last->next = buf;
    _lru_last = buf;
  }
  else
  {
    _lru_first = buf;
    _lru_last = buf;
  }
  _lru_index.insert(
    std::make_pair(std::reference_wrapper<const std::string>(_lru_last->key),
    std::reference_wrapper<lru_node>(*_lru_last)));
  _size += size_of_new_elem;
    return true;
}

void SimpleLRU::_up(lru_node& node)
{
  if(_lru_last != &node)
  {
    if(_lru_first != &node)
    {
      node.prev->next = node.next;
      node.next->prev = node.prev;
      node.prev = _lru_last;
      node.next = nullptr;
      _lru_last->next = &node;
      _lru_last = _lru_last->next;
    }
    else
    {
      _lru_first = node.next;
      _lru_first->prev = nullptr;
      node.prev = _lru_last;
      node.next = nullptr;
      _lru_last->next = &node;
      _lru_last = _lru_last->next;
    }
  }




  //TODO хвост
  // элемент не последний
  /*
  if(node.next != nullptr)
  {
    if(node.prev != _lru_first)
    {
      lru_node *left = node.prev, *mid = node.prev->next, *right = node.next;
      lru_node *right2 = node.next->next;
      left->next = right;
      right->prev = left;
      right->next = mid;
      mid->prev = right;
      mid->next = right2;
      if(right2 != nullptr)
      {
        right2->prev = mid;
      }
      else
      {
        _lru_last = mid;
      }
    }
    else
    {
      lru_node *left = node.prev, *mid = node.prev->next, *right = node.next;
      if(mid != _lru_last)
      {
        left->next = right;
        right->prev = left;
        mid->next = left;
        left->prev = mid;
        mid->prev = nullptr;
        _lru_first = mid;
      }
      else
      {
        mid->next = left;
        left->prev = mid;
        left->next = nullptr;
        mid->prev = nullptr;
        _lru_first = mid;
        _lru_last = left;
      }
    }
  }
  */
  /*
  // Элемент не первый
  if (old_node.prev != nullptr)
  {
    // Элемент не является вторым
    if(old_node.prev->prev != nullptr)
    {
      old_node.prev->next = old_node.next;
      old_node.next->prev = old_node.prev;
      old_node.prev->prev->next = &old_node;
      old_node.next = old_node.prev;
      old_node.prev = old_node.prev->prev;
      old_node.prev->prev = &old_node;
    }
    else
    {
      old_node.prev->next = old_node.next;
      old_node.next->prev = old_node.prev;
      old_node.next = old_node.prev;
      _lru_first = _lru_first->next;
      old_node.prev = nullptr;
    }
  }
  */
}




// See MapBasedGlobalLockImpl.h
bool SimpleLRU::Put(const std::string &key, const std::string &value)
{
    auto it = _lru_index.find(key);
    if (it == _lru_index.end())
    {
      _add(key);
      return _put(key, value);
    }
    else
    {
      lru_node& old_node = it->second;
      old_node.value = value;
      _size += (value.size() - old_node.value.size());
      _up(old_node);
      return true;
    }
}

// See MapBasedGlobalLockImpl.h
bool SimpleLRU::PutIfAbsent(const std::string &key, const std::string &value)
{
  if (_lru_index.find(key) == _lru_index.end())
  {
    _add(key);
    return _put(key, value);
  }
  else
  {
    return false;
  }
}

// See MapBasedGlobalLockImpl.h
bool SimpleLRU::Set(const std::string &key, const std::string &value)
{
  if(!_possiblyContains(key)) return false;
  auto it = _lru_index.find(key);
  if (it == _lru_index.end())
  {
    return false;
  }
  else
  {
    lru_node& old_node = it->second;
    old_node.value = value;
    _size += (value.size() - old_node.value.size());
    _up(old_node);
    return true;
  }
}


// See MapBasedGlobalLockImpl.h
bool SimpleLRU::Delete(const std::string &key)
{
  if(!_possiblyContains(key)) return false;
  auto it = _lru_index.find(key);
  if (it == _lru_index.end())
  {
    return false;
  }
  else
  {
    lru_node& node = it->second;
    if (node.next != nullptr)
    {
      node.next->prev = node.prev;
    }
    if (node.prev != nullptr)
    {
      lru_node* buf = node.prev->next;
      node.prev->next = node.next;
      delete buf;
    }
    // Если это первый узел в списке
    else
    {
      lru_node* buf = _lru_first;
      _lru_first = node.next;
      delete buf;
    }

    _size -= (node.key.size() + node.value.size());
    _lru_index.erase(it);
    return true;
  }
}

// See MapBasedGlobalLockImpl.h
bool SimpleLRU::Get(const std::string &key, std::string &value)
{
  if(!_possiblyContains(key)) return false;
  auto it = _lru_index.find(key);
    if (it == _lru_index.end()) {
        return false;
    }
    else
    {
      value = it->second.get().value;
      lru_node& node = it->second;
      _up(node);
      return true;
    }
 }


bool SimpleLRU::_possiblyContains(const std::string& s)
{
  if(_bits[_hash1(s)] && _bits[_hash2(s)] && _bits[_hash3(s)])
    return true;
  return false;
}

void SimpleLRU::_add(const std::string& s)
{
  _bits[_hash1(s)] = true;
  _bits[_hash2(s)] = true;
  _bits[_hash3(s)] = true;
}

void SimpleLRU::_reupdate()
{
  _bits.resize(_filter, false);
  lru_node* buf = _lru_first;
  while(buf != nullptr)
  {
    _add(buf->key);
    buf = buf->next;
  }
}






// filter`s hashs
size_t SimpleLRU::_hash1(const std::string& s)
{
    const char* str = s.c_str();
    unsigned int hash = 0;

    for (; *str; str++)
    {
        hash += (unsigned char)(*str);
        hash += (hash << 10);
        hash ^= (hash >> 6);
    }
    hash += (hash << 3);
    hash ^= (hash >> 11);
    hash += (hash << 15);

    return hash%_filter;
}

size_t SimpleLRU::_hash2(const std::string& s)
{
  const char* str = s.c_str();
  unsigned int hash = 0;

  for(; *str; str++)
  {
    hash += (unsigned char)(*str);
    hash -= (hash << 13) | (hash >> 19);
  }

  return hash%_filter;
}

size_t SimpleLRU::_hash3(const std::string& s)
{
  const char* str = s.c_str();
  static const unsigned int b = 378551;
  unsigned int a = 63689;
  unsigned int hash = 0;

  for(; *str; str++)
  {
    hash = hash * a + (unsigned char)(*str);
    a *= b;
  }

  return hash%_filter;
}



} // namespace Backend
} // namespace Afina
