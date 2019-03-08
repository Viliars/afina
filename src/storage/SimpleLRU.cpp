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
    lru_node& node = *_lru_first;
    std::unique_ptr<lru_node> bufer; // To auto delete
    if (node.next)
    {
        node.next.get()->prev = nullptr;
    }
    bufer.swap(_lru_first);
    _lru_first = std::move(node.next);
    _lru_index.erase(node.key);
    _size -= (node.key.size() + node.value.size());
  }
  std::unique_ptr<lru_node> bufer{new lru_node{key, value}};
  if(_lru_first.get() != nullptr)
  {
    bufer->prev = _lru_last;
    _lru_last->next.swap(bufer);
    _lru_last = _lru_last->next.get();
  }
  else
  {
    _lru_first.swap(bufer);
    _lru_last = _lru_first.get();
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
    if(_lru_first.get() != &node)
    {
      node.next->prev = node.prev;
      node.prev->next.swap(node.next);
      _lru_last->next.swap(node.next);
      node.prev = _lru_last;
      _lru_last = &node;
    }
    // если надо upнуть _lru_first
    else
    {
      _lru_first.swap(node.next);
      _lru_first->prev = nullptr;
      _lru_last->next.swap(node.next);
      node.prev = _lru_last;
      _lru_last = &node;
    }
  }
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
    std::unique_ptr<lru_node> bufer; // Для авто удаления
    if (node.prev != nullptr)
    {
      bufer.swap(node.prev->next);
      node.prev->next = std::move(node.next);
    }
    // Если это первый узел в списке
    else
    {
      bufer.swap(_lru_first);
      _lru_first = std::move(node.next);
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
  lru_node* bufer = _lru_first.get();
  while (bufer->next.get() != nullptr) {
      _add(bufer->key);
      bufer = bufer->next.get();
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
