#include "vcs/diff/myers.hpp"

#include <cstdint>
#include <stdexcept>
#include <unordered_map>
#include <utility>

/*
 * The edit graph has the old lines along x and the new lines along y. A step
 * right deletes an old line, a step down inserts a new one, and a diagonal step
 * (a "snake") is free where the two lines are equal. Diagonal k holds the
 * points where x - y = k.
 *
 * Instead of saving every round of the search to trace the path back, which
 * takes memory growing with D squared, this searches from both corners at once
 * until the two searches meet. The meeting point splits the problem into two
 * halves that are solved the same way.
 * */

namespace {

typedef std::vector<uint32_t> LineIds;
typedef int64_t Pos;

class Differ {
private:
  const LineIds &a;
  const LineIds &b;
  std::vector<bool> &deleted;
  std::vector<bool> &inserted;

  // A point on a shortest path through a[alo, ahi) x b[blo, bhi)
  std::pair<Pos, Pos> middle(Pos alo, Pos ahi, Pos blo, Pos bhi) const {
    const Pos n = ahi - alo;
    const Pos m = bhi - blo;
    // The searches meet within half the longest possible path. Diagonals run
    // from -max_d to max_d, and each step looks one diagonal either side.
    const Pos max_d = (n + m + 1) / 2 + 1;
    const Pos offset = max_d;
    const Pos size = 2 * max_d + 1;
    const Pos delta = n - m;

    // With an odd delta the forward search is the one that meets the other
    const bool forward_meets = (delta % 2) != 0;

    // Furthest x reached on each diagonal, forward from the top left and
    // backward from the bottom right. -1 means not reached yet.
    std::vector<Pos> forward(size, -1);
    std::vector<Pos> backward(size, -1);
    forward[offset + 1] = 0;
    backward[offset + 1] = 0;

    // Diagonals that ran off the grid aren't worth searching again
    Pos forward_start = 0, forward_end = 0;
    Pos backward_start = 0, backward_end = 0;

    for (Pos d = 0; d < max_d; d++) {
      for (Pos k = -d + forward_start; k <= d - forward_end; k += 2) {
        Pos index = offset + k;
        Pos x;
        if (k == -d || (k != d && forward[index - 1] < forward[index + 1]))
          x = forward[index + 1]; // step down from diagonal k + 1
        else
          x = forward[index - 1] + 1; // step right from diagonal k - 1
        Pos y = x - k;

        while (x < n && y < m && a[alo + x] == b[blo + y]) {
          x++;
          y++;
        }
        forward[index] = x;

        if (x > n) {
          forward_end += 2;
        } else if (y > m) {
          forward_start += 2;
        } else if (forward_meets) {
          Pos other = offset + delta - k;
          if (other >= 0 && other < size && backward[other] != -1 &&
              x >= n - backward[other])
            return {alo + x, blo + y};
        }
      }

      for (Pos k = -d + backward_start; k <= d - backward_end; k += 2) {
        Pos index = offset + k;
        Pos x;
        if (k == -d || (k != d && backward[index - 1] < backward[index + 1]))
          x = backward[index + 1];
        else
          x = backward[index - 1] + 1;
        Pos y = x - k;

        // the same search mirrored: x and y count back from the far corner
        while (x < n && y < m && a[ahi - 1 - x] == b[bhi - 1 - y]) {
          x++;
          y++;
        }
        backward[index] = x;

        if (x > n) {
          backward_end += 2;
        } else if (y > m) {
          backward_start += 2;
        } else if (!forward_meets) {
          Pos other = offset + delta - k;
          if (other >= 0 && other < size && forward[other] != -1) {
            Pos fx = forward[other];
            Pos fy = fx - (other - offset);
            if (fx >= n - x)
              return {alo + fx, blo + fy};
          }
        }
      }
    }

    throw std::logic_error("Myers search ended without the halves meeting");
  }

public:
  Differ(const LineIds &a_in, const LineIds &b_in, std::vector<bool> &deleted_in,
         std::vector<bool> &inserted_in)
      : a(a_in), b(b_in), deleted(deleted_in), inserted(inserted_in) {}

  void compare(Pos alo, Pos ahi, Pos blo, Pos bhi) {
    // Matching lines at either end are kept, and cost nothing to find
    while (alo < ahi && blo < bhi && a[alo] == b[blo]) {
      alo++;
      blo++;
    }
    while (alo < ahi && blo < bhi && a[ahi - 1] == b[bhi - 1]) {
      ahi--;
      bhi--;
    }

    if (alo == ahi) {
      for (Pos y = blo; y < bhi; y++)
        inserted[y] = true;
      return;
    }

    if (blo == bhi) {
      for (Pos x = alo; x < ahi; x++)
        deleted[x] = true;
      return;
    }

    auto [x, y] = middle(alo, ahi, blo, bhi);

    // Both ends were trimmed, so a split at a corner would recurse forever
    if ((x == alo && y == blo) || (x == ahi && y == bhi))
      throw std::logic_error("Myers split made no progress");

    compare(alo, x, blo, y);
    compare(x, ahi, y, bhi);
  }
};

} // namespace

std::vector<std::string> vcs::split_lines(std::string_view text) {
  std::vector<std::string> lines;
  size_t start = 0;

  while (start < text.size()) {
    size_t end = text.find('\n', start);
    end = (end == std::string_view::npos) ? text.size() : end + 1;
    lines.emplace_back(text.substr(start, end - start));
    start = end;
  }

  return lines;
}

std::vector<vcs::DiffOp>
vcs::diff_lines(const std::vector<std::string> &old_lines,
                const std::vector<std::string> &new_lines) {
  // Comparing numbers is far cheaper than comparing strings
  std::unordered_map<std::string_view, uint32_t> ids;
  auto to_ids = [&ids](const std::vector<std::string> &lines) {
    LineIds out;
    out.reserve(lines.size());
    for (const std::string &line : lines)
      out.push_back(
          ids.try_emplace(line, static_cast<uint32_t>(ids.size())).first->second);
    return out;
  };

  LineIds a = to_ids(old_lines);
  LineIds b = to_ids(new_lines);

  std::vector<bool> in_a(ids.size(), false);
  std::vector<bool> in_b(ids.size(), false);
  for (uint32_t id : a)
    in_a[id] = true;
  for (uint32_t id : b)
    in_b[id] = true;

  // A line that never appears in the other file can't be kept, so it is
  // deleted or inserted without searching. The edit script stays shortest,
  // and a file that was rewritten completely leaves nothing to search.
  std::vector<bool> deleted(a.size(), false);
  std::vector<bool> inserted(b.size(), false);

  LineIds a_left, b_left;
  std::vector<size_t> a_where, b_where;

  for (size_t i = 0; i < a.size(); i++) {
    if (in_b[a[i]]) {
      a_left.push_back(a[i]);
      a_where.push_back(i);
    } else {
      deleted[i] = true;
    }
  }

  for (size_t j = 0; j < b.size(); j++) {
    if (in_a[b[j]]) {
      b_left.push_back(b[j]);
      b_where.push_back(j);
    } else {
      inserted[j] = true;
    }
  }

  std::vector<bool> left_deleted(a_left.size(), false);
  std::vector<bool> left_inserted(b_left.size(), false);

  Differ(a_left, b_left, left_deleted, left_inserted)
      .compare(0, static_cast<Pos>(a_left.size()), 0,
               static_cast<Pos>(b_left.size()));

  for (size_t i = 0; i < a_left.size(); i++)
    if (left_deleted[i])
      deleted[a_where[i]] = true;

  for (size_t j = 0; j < b_left.size(); j++)
    if (left_inserted[j])
      inserted[b_where[j]] = true;

  std::vector<DiffOp> ops;
  ops.reserve(a.size() + b.size());

  size_t i = 0, j = 0;
  while (i < a.size() || j < b.size()) {
    if (i < a.size() && deleted[i]) {
      ops.push_back({DiffOpType::Delete, i++, j});
    } else if (j < b.size() && inserted[j]) {
      ops.push_back({DiffOpType::Insert, i, j++});
    } else {
      ops.push_back({DiffOpType::Equal, i++, j++});
    }
  }

  return ops;
}
