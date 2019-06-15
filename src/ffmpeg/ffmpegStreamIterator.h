#pragma once

#include <unordered_map>

namespace ffmpeg
{

template <typename stream_t, typename bidir_iterator = std::unordered_map<int, stream_t *>::iterator>
class StreamIterator
{
  public:
  typedef typename int key_type;
  typedef typename stream_t &mapped_type;
  typedef typename std::pair<const key_type, mapped_type> value_type;

  StreamIterator(bidir_iterator &map_it) : map_iter(map_it) {}
  bool operator==(const StreamIterator &i) { return i.map_iter == map_iter; }
  bool operator!=(const StreamIterator &i) { return i.map_iter != map_iter; }
  StreamIterator &operator++()
  {
    ++map_iter;
    return *this;
  }
  StreamIterator &operator--()
  {
    --map_iter;
    return *this;
  }
  value_type operator*() const { return std::make_pair(map_iter->first, *map_iter->second); }

  private:
  bidir_iterator map_iter;
};

} // namespace ffmpeg
