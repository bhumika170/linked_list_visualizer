#include <algorithm>
#include <cstdint>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <optional>
#include <regex>
#include <sstream>
#include <string>
#include <unordered_set>
#include <utility>
#include <vector>

// ---------- tiny JSON helpers (emit only; parse via regex for known keys) ----------
static std::string jsonEscape(const std::string &s) {
  std::string out;
  out.reserve(s.size() + 8);
  for (char c : s) {
    switch (c) {
    case '\\': out += "\\\\"; break;
    case '"': out += "\\\""; break;
    case '\n': out += "\\n"; break;
    case '\r': out += "\\r"; break;
    case '\t': out += "\\t"; break;
    default: out += c; break;
    }
  }
  return out;
}

static std::string readAllStdin() {
  std::ostringstream ss;
  ss << std::cin.rdbuf();
  return ss.str();
}

static bool readFileToString(const std::string &path, std::string &out) {
  std::ifstream f(path);
  if (!f.is_open()) return false;
  std::ostringstream ss;
  ss << f.rdbuf();
  out = ss.str();
  return true;
}

static bool writeStringToFile(const std::string &path, const std::string &data) {
  std::ofstream f(path, std::ios::trunc);
  if (!f.is_open()) return false;
  f << data;
  return true;
}

static bool fileExists(const std::string &path) {
  std::ifstream f(path);
  return f.good();
}

static std::optional<std::string> extractJsonString(const std::string &json, const std::string &key) {
  const std::regex re("\"" + key + "\"\\s*:\\s*\"([^\"]*)\"");
  std::smatch m;
  if (std::regex_search(json, m, re) && m.size() >= 2) return m[1].str();
  return std::nullopt;
}

static std::optional<long long> extractJsonInt(const std::string &json, const std::string &key) {
  const std::regex re("\"" + key + "\"\\s*:\\s*(-?\\d+)");
  std::smatch m;
  if (std::regex_search(json, m, re) && m.size() >= 2) return std::stoll(m[1].str());
  return std::nullopt;
}

static std::vector<int> extractNodeValuesFromStateJson(const std::string &stateJson) {
  std::vector<int> values;
  const std::regex re("\"value\"\\s*:\\s*(-?\\d+)");
  for (auto it = std::sregex_iterator(stateJson.begin(), stateJson.end(), re);
       it != std::sregex_iterator(); ++it) {
    values.push_back(std::stoi((*it)[1].str()));
  }
  return values;
}

static std::string ptrHex(const void *p) {
  std::uintptr_t v = reinterpret_cast<std::uintptr_t>(p);
  std::ostringstream ss;
  ss << "0x" << std::hex << std::uppercase << v;
  return ss.str();
}

// ---------- linked list logic (real pointers) ----------
enum class ListType { Singly, Doubly, Circular };

static std::string listTypeToString(ListType t) {
  switch (t) {
  case ListType::Singly: return "singly";
  case ListType::Doubly: return "doubly";
  case ListType::Circular: return "circular";
  }
  return "singly";
}

static ListType parseListType(const std::string &s) {
  if (s == "doubly") return ListType::Doubly;
  if (s == "circular") return ListType::Circular;
  return ListType::Singly;
}

struct Node {
  int value{};
  Node *next{nullptr};
  Node *prev{nullptr}; // used for doubly
  explicit Node(int v) : value(v) {}
};

struct Meta {
  std::string message;
  std::string complexity;
  std::vector<int> steps;
  int highlightIndex = -1;
  int searchResultIndex = -1;
  bool loopDetected = false;
};

class LinkedList {
public:
  explicit LinkedList(ListType t) : type_(t) {}
  ~LinkedList() { clear(); }

  LinkedList(const LinkedList &) = delete;
  LinkedList &operator=(const LinkedList &) = delete;

  LinkedList(LinkedList &&o) noexcept { moveFrom(std::move(o)); }
  LinkedList &operator=(LinkedList &&o) noexcept {
    if (this != &o) {
      clear();
      moveFrom(std::move(o));
    }
    return *this;
  }

  void setType(ListType t) {
    if (t == type_) return;
    // Keep same nodes/values, only adapt invariants.
    if (t == ListType::Circular) {
      type_ = t;
      fixCircularLinks();
    } else {
      // break circular if currently circular
      if (type_ == ListType::Circular) breakCircularLinks();
      type_ = t;
      if (type_ == ListType::Doubly) rebuildPrevLinks();
      else clearPrevLinks();
    }
  }

  ListType type() const { return type_; }
  int size() const { return size_; }
  Node *head() const { return head_; }
  Node *tail() const { return tail_; }

  void clear() {
    if (!head_) {
      head_ = tail_ = nullptr;
      size_ = 0;
      return;
    }

    // To avoid infinite loops, use visited set.
    std::unordered_set<Node *> seen;
    Node *cur = head_;
    while (cur && !seen.count(cur)) {
      seen.insert(cur);
      Node *nxt = cur->next;
      delete cur;
      cur = nxt;
    }
    head_ = tail_ = nullptr;
    size_ = 0;
  }

  bool insertAtBeginning(int v) {
    Node *n = new Node(v);
    if (!head_) {
      head_ = tail_ = n;
      size_ = 1;
      if (type_ == ListType::Circular) fixCircularLinks();
      return true;
    }
    if (type_ == ListType::Circular) breakCircularLinks();

    n->next = head_;
    if (type_ == ListType::Doubly) {
      head_->prev = n;
      n->prev = nullptr;
    }
    head_ = n;
    size_++;

    if (type_ == ListType::Circular) fixCircularLinks();
    return true;
  }

  bool insertAtEnd(int v) {
    Node *n = new Node(v);
    if (!head_) {
      head_ = tail_ = n;
      size_ = 1;
      if (type_ == ListType::Circular) fixCircularLinks();
      return true;
    }
    if (type_ == ListType::Circular) breakCircularLinks();

    tail_->next = n;
    if (type_ == ListType::Doubly) {
      n->prev = tail_;
    }
    tail_ = n;
    size_++;

    if (type_ == ListType::Circular) fixCircularLinks();
    return true;
  }

  bool insertAtPosition(int v, int pos) {
    if (pos < 0 || pos > size_) return false;
    if (pos == 0) return insertAtBeginning(v);
    if (pos == size_) return insertAtEnd(v);

    if (type_ == ListType::Circular) breakCircularLinks();
    Node *cur = head_;
    for (int i = 0; i < pos - 1; i++) cur = cur->next;
    Node *n = new Node(v);
    n->next = cur->next;
    cur->next = n;
    if (type_ == ListType::Doubly) {
      n->prev = cur;
      if (n->next) n->next->prev = n;
    }
    size_++;
    if (pos == size_ - 1) tail_ = n;

    if (type_ == ListType::Circular) fixCircularLinks();
    return true;
  }

  bool deleteAtBeginning() {
    if (!head_) return false;
    if (type_ == ListType::Circular) breakCircularLinks();

    Node *del = head_;
    head_ = head_->next;
    if (type_ == ListType::Doubly && head_) head_->prev = nullptr;
    delete del;
    size_--;
    if (size_ == 0) tail_ = nullptr;
    if (type_ == ListType::Circular) fixCircularLinks();
    return true;
  }

  bool deleteAtEnd() {
    if (!head_) return false;
    if (size_ == 1) {
      delete head_;
      head_ = tail_ = nullptr;
      size_ = 0;
      return true;
    }
    if (type_ == ListType::Circular) breakCircularLinks();

    if (type_ == ListType::Doubly) {
      Node *del = tail_;
      tail_ = tail_->prev;
      tail_->next = nullptr;
      delete del;
      size_--;
      if (type_ == ListType::Circular) fixCircularLinks();
      return true;
    }

    // singly: traverse to node before tail
    Node *cur = head_;
    while (cur->next && cur->next != tail_) cur = cur->next;
    Node *del = tail_;
    cur->next = nullptr;
    tail_ = cur;
    delete del;
    size_--;
    if (type_ == ListType::Circular) fixCircularLinks();
    return true;
  }

  bool deleteAtPosition(int pos) {
    if (pos < 0 || pos >= size_) return false;
    if (pos == 0) return deleteAtBeginning();
    if (pos == size_ - 1) return deleteAtEnd();

    if (type_ == ListType::Circular) breakCircularLinks();
    Node *cur = head_;
    for (int i = 0; i < pos - 1; i++) cur = cur->next;
    Node *del = cur->next;
    cur->next = del->next;
    if (type_ == ListType::Doubly) {
      if (del->next) del->next->prev = cur;
    }
    delete del;
    size_--;
    if (type_ == ListType::Circular) fixCircularLinks();
    return true;
  }

  int search(int v, std::vector<int> &steps) const {
    steps.clear();
    if (!head_) return -1;

    // Safe traversal even if circular: limit to size_ steps.
    Node *cur = head_;
    for (int i = 0; i < size_; i++) {
      steps.push_back(i);
      if (cur && cur->value == v) return i;
      cur = cur ? cur->next : nullptr;
    }
    return -1;
  }

  void reverse(std::vector<int> &steps) {
    steps.clear();
    for (int i = 0; i < size_; i++) steps.push_back(i);
    if (size_ <= 1) return;

    const bool wasCircular = (type_ == ListType::Circular);
    if (wasCircular) breakCircularLinks();

    if (type_ == ListType::Doubly) {
      Node *cur = head_;
      Node *tmp = nullptr;
      while (cur) {
        tmp = cur->prev;
        cur->prev = cur->next;
        cur->next = tmp;
        cur = cur->prev;
      }
      // swap head/tail
      tmp = head_;
      head_ = tail_;
      tail_ = tmp;
    } else {
      Node *prev = nullptr;
      Node *cur = head_;
      Node *next = nullptr;
      tail_ = head_;
      while (cur) {
        next = cur->next;
        cur->next = prev;
        prev = cur;
        cur = next;
      }
      head_ = prev;
      if (type_ == ListType::Doubly) rebuildPrevLinks();
    }

    if (wasCircular) fixCircularLinks();
  }

  bool detectLoop() const {
    // Real Floyd cycle detection on next pointers.
    Node *slow = head_;
    Node *fast = head_;
    while (fast && fast->next) {
      slow = slow->next;
      fast = fast->next->next;
      if (slow && slow == fast) return true;
    }
    return false;
  }

  // Snapshot for save/undo (values only; addresses are pointer-based and will differ after restore)
  std::vector<int> toValues() const {
    std::vector<int> out;
    out.reserve(std::max(0, size_));
    Node *cur = head_;
    for (int i = 0; i < size_; i++) {
      out.push_back(cur ? cur->value : 0);
      cur = cur ? cur->next : nullptr;
    }
    return out;
  }

  void fromValues(const std::vector<int> &vals) {
    clear();
    for (int v : vals) insertAtEnd(v);
  }

  // For visualization: nodes in order (bounded by size_)
  std::vector<std::pair<int, std::string>> nodesForJson() const {
    std::vector<std::pair<int, std::string>> out;
    out.reserve(std::max(0, size_));
    Node *cur = head_;
    for (int i = 0; i < size_; i++) {
      out.push_back({cur ? cur->value : 0, ptrHex(cur)});
      cur = cur ? cur->next : nullptr;
    }
    return out;
  }

private:
  void breakCircularLinks() {
    if (type_ != ListType::Circular) return;
    if (tail_ && tail_->next == head_) tail_->next = nullptr;
  }

  void fixCircularLinks() {
    if (type_ != ListType::Circular) return;
    if (!head_) return;
    // ensure tail_ correct
    if (!tail_) {
      tail_ = head_;
    } else {
      // If tail_->next is nullptr (after breaking), keep tail_ as is.
      // If tail_ is stale, recompute.
      Node *cur = head_;
      for (int i = 1; i < size_; i++) cur = cur->next;
      tail_ = cur;
    }
    if (tail_) tail_->next = head_;
  }

  void rebuildPrevLinks() {
    if (type_ != ListType::Doubly) return;
    Node *prev = nullptr;
    Node *cur = head_;
    for (int i = 0; i < size_; i++) {
      if (!cur) break;
      cur->prev = prev;
      prev = cur;
      cur = cur->next;
    }
    tail_ = prev ? prev : tail_;
  }

  void clearPrevLinks() {
    Node *cur = head_;
    for (int i = 0; i < size_; i++) {
      if (!cur) break;
      cur->prev = nullptr;
      cur = cur->next;
    }
  }

  void moveFrom(LinkedList &&o) {
    type_ = o.type_;
    head_ = o.head_;
    tail_ = o.tail_;
    size_ = o.size_;
    o.head_ = o.tail_ = nullptr;
    o.size_ = 0;
  }

  ListType type_;
  Node *head_{nullptr};
  Node *tail_{nullptr};
  int size_{0};
};

// ---------- undo log helpers ----------
static void appendUndoSnapshot(const std::string &undoLogPath, const std::string &snapshotJson) {
  std::ofstream f(undoLogPath, std::ios::app);
  if (!f.is_open()) return;
  std::string oneLine = snapshotJson;
  oneLine.erase(std::remove(oneLine.begin(), oneLine.end(), '\n'), oneLine.end());
  f << oneLine << "\n";
}

static std::optional<std::string> popLastUndoSnapshot(const std::string &undoLogPath) {
  std::string all;
  if (!readFileToString(undoLogPath, all)) return std::nullopt;
  if (all.empty()) return std::nullopt;
  while (!all.empty() && (all.back() == '\n' || all.back() == '\r')) all.pop_back();
  if (all.empty()) return std::nullopt;

  size_t lastNl = all.find_last_of('\n');
  std::string lastLine;
  std::string remaining;
  if (lastNl == std::string::npos) {
    lastLine = all;
    remaining = "";
  } else {
    lastLine = all.substr(lastNl + 1);
    remaining = all.substr(0, lastNl + 1);
  }
  writeStringToFile(undoLogPath, remaining);
  return lastLine;
}

static std::string complexityForOp(const std::string &op) {
  if (op == "insertAtBeginning") return "O(1)";
  if (op == "insertAtEnd") return "O(1) (tail known)";
  if (op == "insertAtPosition") return "O(n)";
  if (op == "deleteAtBeginning") return "O(1)";
  if (op == "deleteAtEnd") return "O(n) (singly) / O(1) (doubly)";
  if (op == "deleteAtPosition") return "O(n)";
  if (op == "search") return "O(n)";
  if (op == "reverse") return "O(n)";
  if (op == "detectLoop") return "O(n) (Floyd)";
  if (op == "clear") return "O(n)";
  if (op == "undo") return "O(n)";
  if (op == "save") return "O(n)";
  if (op == "load") return "O(n)";
  return "O(?)";
}

static std::string emitStateJson(const LinkedList &list, const Meta &meta) {
  auto nodes = list.nodesForJson();
  std::ostringstream ss;
  ss << "{\n";
  ss << "  \"type\": \"" << jsonEscape(listTypeToString(list.type())) << "\",\n";
  ss << "  \"nodes\": [\n";
  for (int i = 0; i < static_cast<int>(nodes.size()); i++) {
    ss << "    {\"value\": " << nodes[i].first << ", \"address\": \"" << jsonEscape(nodes[i].second) << "\"}";
    if (i != static_cast<int>(nodes.size()) - 1) ss << ",";
    ss << "\n";
  }
  ss << "  ],\n";
  ss << "  \"head\": " << (list.size() == 0 ? -1 : 0) << ",\n";
  ss << "  \"tail\": " << (list.size() == 0 ? -1 : (list.size() - 1)) << ",\n";
  ss << "  \"meta\": {\n";
  ss << "    \"message\": \"" << jsonEscape(meta.message) << "\",\n";
  ss << "    \"complexity\": \"" << jsonEscape(meta.complexity) << "\",\n";
  ss << "    \"highlightIndex\": " << meta.highlightIndex << ",\n";
  ss << "    \"searchResultIndex\": " << meta.searchResultIndex << ",\n";
  ss << "    \"loopDetected\": " << (meta.loopDetected ? "true" : "false") << ",\n";
  ss << "    \"steps\": [";
  for (size_t i = 0; i < meta.steps.size(); i++) {
    ss << meta.steps[i];
    if (i + 1 != meta.steps.size()) ss << ", ";
  }
  ss << "]\n";
  ss << "  }\n";
  ss << "}\n";
  return ss.str();
}

int main() {
  const std::string statePath = "state.json";
  const std::string undoLogPath = "undo.log";

  const std::string req = readAllStdin();
  const std::string op = extractJsonString(req, "op").value_or("noop");
  const std::string typeStr = extractJsonString(req, "type").value_or("singly");
  const int value = static_cast<int>(extractJsonInt(req, "value").value_or(0));
  const int position = static_cast<int>(extractJsonInt(req, "position").value_or(-1));
  const std::string file = extractJsonString(req, "file").value_or("sample_state.json");

  std::string prevStateJson;
  const bool hasPrev = readFileToString(statePath, prevStateJson);
  ListType prevType = parseListType(typeStr);
  if (hasPrev) prevType = parseListType(extractJsonString(prevStateJson, "type").value_or(typeStr));

  LinkedList list(prevType);
  if (hasPrev) {
    auto vals = extractNodeValuesFromStateJson(prevStateJson);
    list.fromValues(vals);
  }
  list.setType(parseListType(typeStr)); // dropdown type drives semantics

  Meta meta;
  meta.complexity = complexityForOp(op);

  const bool mutates =
      (op == "clear") || (op == "insertAtBeginning") || (op == "insertAtEnd") ||
      (op == "insertAtPosition") || (op == "deleteAtBeginning") || (op == "deleteAtEnd") ||
      (op == "deleteAtPosition") || (op == "reverse");

  if (op == "undo") {
    auto snapshot = popLastUndoSnapshot(undoLogPath);
    if (!snapshot.has_value()) {
      meta.message = "Nothing to undo.";
    } else {
      auto vals = extractNodeValuesFromStateJson(*snapshot);
      list.fromValues(vals);
      // keep current dropdown type, but restore values
      list.setType(parseListType(typeStr));
      meta.message = "Undid last operation.";
    }
  } else if (op == "load") {
    std::string loaded;
    if (!readFileToString(file, loaded)) {
      meta.message = "Load failed: could not read file '" + file + "'.";
    } else {
      if (hasPrev) appendUndoSnapshot(undoLogPath, prevStateJson);
      auto vals = extractNodeValuesFromStateJson(loaded);
      list.fromValues(vals);
      list.setType(parseListType(typeStr));
      meta.message = "Loaded from '" + file + "'.";
    }
  } else {
    if (hasPrev && mutates) appendUndoSnapshot(undoLogPath, prevStateJson);

    if (op == "clear") {
      list.clear();
      meta.message = "Cleared list.";
    } else if (op == "insertAtBeginning") {
      list.insertAtBeginning(value);
      meta.message = "Inserted at beginning: " + std::to_string(value);
      meta.highlightIndex = 0;
    } else if (op == "insertAtEnd") {
      list.insertAtEnd(value);
      meta.message = "Inserted at end: " + std::to_string(value);
      meta.highlightIndex = list.size() - 1;
    } else if (op == "insertAtPosition") {
      if (!list.insertAtPosition(value, position)) {
        meta.message = "Insert failed: invalid position " + std::to_string(position) + ".";
      } else {
        meta.message = "Inserted " + std::to_string(value) + " at position " + std::to_string(position) + ".";
        meta.highlightIndex = position;
        for (int i = 0; i <= position && i < list.size(); i++) meta.steps.push_back(i);
      }
    } else if (op == "deleteAtBeginning") {
      if (!list.deleteAtBeginning()) meta.message = "Delete failed: list is empty.";
      else meta.message = "Deleted at beginning.";
      meta.highlightIndex = 0;
    } else if (op == "deleteAtEnd") {
      if (!list.deleteAtEnd()) meta.message = "Delete failed: list is empty.";
      else meta.message = "Deleted at end.";
      meta.highlightIndex = std::max(0, list.size() - 1);
    } else if (op == "deleteAtPosition") {
      if (position < 0 || position >= list.size()) {
        meta.message = "Delete failed: invalid position " + std::to_string(position) + ".";
      } else {
        meta.highlightIndex = position;
        for (int i = 0; i <= position && i < list.size(); i++) meta.steps.push_back(i);
        list.deleteAtPosition(position);
        meta.message = "Deleted at position " + std::to_string(position) + ".";
      }
    } else if (op == "search") {
      int idx = list.search(value, meta.steps);
      meta.searchResultIndex = idx;
      meta.highlightIndex = idx;
      if (idx == -1) meta.message = "Search: " + std::to_string(value) + " not found.";
      else meta.message = "Search: " + std::to_string(value) + " found at index " + std::to_string(idx) + ".";
    } else if (op == "reverse") {
      list.reverse(meta.steps);
      meta.message = "Reversed list.";
      meta.highlightIndex = (list.size() == 0 ? -1 : 0);
    } else if (op == "detectLoop") {
      meta.message = "Detect loop executed.";
    } else if (op == "save") {
      meta.message = "Saved current list to '" + file + "'.";
    } else if (op == "noop") {
      meta.message = "Ready.";
    } else {
      meta.message = "Unknown operation: '" + op + "'.";
    }
  }

  meta.loopDetected = list.detectLoop();
  if (op == "detectLoop") {
    meta.message = std::string("Detect loop: ") + (meta.loopDetected ? "LOOP DETECTED" : "no loop detected");
  }

  std::string out = emitStateJson(list, meta);
  writeStringToFile(statePath, out);
  if (op == "save") writeStringToFile(file, out);
  std::cout << out;
  return 0;
}

