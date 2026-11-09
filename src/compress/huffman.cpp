#include "vcs/compress/huffman.hpp"

#include <algorithm>
#include <functional>
#include <queue>
#include <stdexcept>
#include <utility>

namespace {

constexpr uint8_t MAX_CODE_LENGTH = 15;

struct Node {
  uint64_t freq;
  size_t parent;
  size_t symbol; // only meaningful for leaves
};

// Frequency first, then node index, so ties always break the same way and the
// same input always gives the same lengths
typedef std::pair<uint64_t, size_t> HeapEntry;
typedef std::priority_queue<HeapEntry, std::vector<HeapEntry>,
                            std::greater<HeapEntry>>
    MinHeap;

// Builds a Huffman tree and returns each symbol's depth in it, uncapped. The
// depths can exceed 15, so they are held in a wider type than the final result.
std::vector<size_t> tree_depths(const std::vector<uint64_t> &freqs) {
  std::vector<size_t> depths(freqs.size(), 0);

  std::vector<Node> nodes;
  MinHeap heap;

  for (size_t symbol = 0; symbol < freqs.size(); symbol++) {
    if (freqs[symbol] == 0)
      continue;
    heap.push({freqs[symbol], nodes.size()});
    nodes.push_back({freqs[symbol], 0, symbol});
  }

  if (nodes.empty())
    return depths;

  // a tree of one node has depth 0, but a 0-bit code can't be written
  if (nodes.size() == 1) {
    depths[nodes[0].symbol] = 1;
    return depths;
  }

  size_t leaf_count = nodes.size();

  while (heap.size() > 1) {
    HeapEntry first = heap.top();
    heap.pop();
    HeapEntry second = heap.top();
    heap.pop();

    size_t parent = nodes.size();
    uint64_t sum = first.first + second.first;

    nodes[first.second].parent = parent;
    nodes[second.second].parent = parent;
    nodes.push_back({sum, 0, 0});
    heap.push({sum, parent});
  }

  // Parents are always created after their children, so walking backwards
  // from the root reaches every parent before its children
  std::vector<size_t> node_depth(nodes.size(), 0);
  for (size_t i = nodes.size() - 1; i-- > 0;)
    node_depth[i] = node_depth[nodes[i].parent] + 1;

  for (size_t i = 0; i < leaf_count; i++)
    depths[nodes[i].symbol] = node_depth[i];

  return depths;
}

} // namespace

std::vector<uint8_t>
vcs::huffman_code_lengths(const std::vector<uint64_t> &freqs) {
  if (freqs.empty())
    return {};

  // 2^15 symbols is the most that fit in 15 bits, even in a balanced tree
  if (freqs.size() > (size_t(1) << MAX_CODE_LENGTH))
    throw std::invalid_argument("Too many symbols for 15-bit codes");

  std::vector<uint64_t> scaled = freqs;
  std::vector<size_t> depths = tree_depths(scaled);

  // Too deep: flatten the frequencies and rebuild. All frequencies reach 1
  // eventually, which gives a balanced tree, so this always finishes. Every
  // attempt is a real Huffman tree, so the lengths never need repairing.
  while (*std::max_element(depths.begin(), depths.end()) > MAX_CODE_LENGTH) {
    for (uint64_t &freq : scaled)
      if (freq > 0)
        freq = std::max<uint64_t>(freq / 2, 1);

    depths = tree_depths(scaled);
  }

  return std::vector<uint8_t>(depths.begin(), depths.end());
}

std::vector<uint32_t> vcs::canonical_codes(const std::vector<uint8_t> &lengths) {
  std::vector<uint32_t> length_counts(MAX_CODE_LENGTH + 1, 0);

  for (uint8_t length : lengths) {
    if (length > MAX_CODE_LENGTH)
      throw std::invalid_argument("Code length must be at most 15");
    if (length > 0)
      length_counts[length]++;
  }

  // The first code of each length comes right after the last code of the
  // length before it, shifted left to make room for the extra bit. The shift
  // is what keeps a longer code from starting with a shorter one.
  std::vector<uint32_t> next_code(MAX_CODE_LENGTH + 1, 0);
  uint32_t code = 0;

  for (size_t length = 1; length <= MAX_CODE_LENGTH; length++) {
    code = (code + length_counts[length - 1]) << 1;
    next_code[length] = code;
  }

  std::vector<uint32_t> codes(lengths.size(), 0);

  for (size_t symbol = 0; symbol < lengths.size(); symbol++)
    if (lengths[symbol] > 0)
      codes[symbol] = next_code[lengths[symbol]]++;

  return codes;
}
