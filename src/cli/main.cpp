#include <ctime>
#include <filesystem>
#include <iostream>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

#include "vcs/diff/unified.hpp"
#include "vcs/objects/blob.hpp"
#include "vcs/objects/object.hpp"
#include "vcs/objects/object_store.hpp"
#include "vcs/objects/tree.hpp"
#include "vcs/repo/repository.hpp"
#include "vcs/util/bytes.hpp"

namespace {

vcs::Repository current_repo() {
  return vcs::Repository::discover(std::filesystem::current_path());
}

void cmd_init(const std::vector<std::string> &args) {
  std::filesystem::path root =
      args.empty() ? std::filesystem::current_path()
                   : std::filesystem::path(args[0]);

  vcs::Repository::init(root);

  std::cout << "Initialized empty vcs repository in "
            << (root / ".vcs").string() << "\n";
}

void cmd_hash_object(const std::vector<std::string> &args) {
  if (args.empty())
    throw std::runtime_error("usage: vcs hash-object <file>");

  vcs::Repository repo = current_repo();
  vcs::Blob blob(args[0]);

  std::cout << repo.objects().write(blob.get_blob()) << "\n";
}

void print_bytes(const vcs::ByteStream &bytes) {
  std::cout.write(reinterpret_cast<const char *>(bytes.data()), bytes.size());
}

void cmd_cat_file(const std::vector<std::string> &args) {
  if (args.empty())
    throw std::runtime_error("usage: vcs cat-file <hash>");

  vcs::Repository repo = current_repo();

  vcs::ByteStream buffer = repo.objects().read(args[0]);
  vcs::UnwrappedObject obj = vcs::unwrap(buffer);

  switch (obj.get_object_type()) {
  case vcs::ObjectType::Blob:
    print_bytes(obj.get_content());
    break;
  case vcs::ObjectType::Commit:
    print_bytes(obj.get_content());
    break;
  case vcs::ObjectType::Tree:
    for (const vcs::TreeNode &entry :
         vcs::parse_tree_entries(obj.get_content())) {
      std::cout << vcs::tree_type_to_string(entry.mode) << " "
                << (entry.mode == vcs::TreeNodeType::Dir ? "tree" : "blob")
                << " " << vcs::to_hex(entry.hash) << "\t" << entry.name
                << "\n";
    }
    break;
  }
}

void cmd_config(const std::vector<std::string> &args) {
  if (args.empty() || args.size() > 2)
    throw std::runtime_error("usage: vcs config <key> [value]");

  vcs::Repository repo = current_repo();

  if (args.size() == 2) {
    repo.set_config(args[0], args[1]);
    return;
  }

  std::optional<std::string> value = repo.get_config(args[0]);
  if (!value)
    throw std::runtime_error("Config key not set: " + args[0]);

  std::cout << *value << "\n";
}

void cmd_add(const std::vector<std::string> &args) {
  if (args.empty())
    throw std::runtime_error("usage: vcs add <path>...");

  current_repo().add({args.begin(), args.end()});
}

std::string required_config(const vcs::Repository &repo,
                            const std::string &key) {
  std::optional<std::string> value = repo.get_config(key);
  if (!value)
    throw std::runtime_error(key + " is not set. Run: vcs config " + key +
                             " <value>");
  return *value;
}

// "Name <email> <unix time> <+hhmm>", the format commit objects use
std::string make_signature(const vcs::Repository &repo) {
  std::time_t now = std::time(nullptr);

  char zone[8];
  std::strftime(zone, sizeof(zone), "%z", std::localtime(&now));

  return required_config(repo, "user.name") + " <" +
         required_config(repo, "user.email") + "> " + std::to_string(now) +
         " " + zone;
}

void cmd_commit(const std::vector<std::string> &args) {
  if (args.size() != 2 || args[0] != "-m")
    throw std::runtime_error("usage: vcs commit -m <message>");

  vcs::Repository repo = current_repo();
  std::string hash = repo.commit(args[1], make_signature(repo));

  std::cout << "[" << hash.substr(0, 7) << "] "
            << args[1].substr(0, args[1].find('\n')) << "\n";
}

// Splits a signature into "Name <email>" and a readable date in its own zone
struct Signature {
  std::string who;
  std::string date;
};

Signature parse_signature(const std::string &signature) {
  size_t zone_space = signature.rfind(' ');
  if (zone_space == std::string::npos || zone_space == 0)
    return {signature, ""};

  size_t time_space = signature.rfind(' ', zone_space - 1);
  if (time_space == std::string::npos)
    return {signature, ""};

  std::string zone = signature.substr(zone_space + 1);
  if (zone.size() != 5)
    return {signature, ""};

  try {
    long long seconds = std::stoll(signature.substr(time_space + 1));
    int hours = std::stoi(zone.substr(1, 2));
    int minutes = std::stoi(zone.substr(3, 2));
    int offset = (hours * 60 + minutes) * 60;

    std::time_t local =
        static_cast<std::time_t>(seconds + (zone[0] == '-' ? -offset : offset));

    char date[64];
    std::strftime(date, sizeof(date), "%a %b %d %H:%M:%S %Y",
                  std::gmtime(&local));

    return {signature.substr(0, time_space), std::string(date) + " " + zone};
  } catch (const std::exception &) {
    return {signature, ""};
  }
}

void cmd_log(const std::vector<std::string> &args) {
  if (!args.empty())
    throw std::runtime_error("usage: vcs log");

  std::vector<vcs::LogEntry> entries = current_repo().log();

  if (entries.empty())
    throw std::runtime_error("No commits yet");

  for (const vcs::LogEntry &entry : entries) {
    Signature author = parse_signature(entry.commit.author);

    std::cout << "commit " << entry.hash << "\n"
              << "Author: " << author.who << "\n"
              << "Date:   " << author.date << "\n\n";

    std::istringstream lines(entry.commit.message);
    std::string line;
    while (std::getline(lines, line))
      std::cout << (line.empty() ? "" : "    ") << line << "\n";

    std::cout << "\n";
  }
}

void print_changes(const std::string &title,
                   const std::vector<vcs::TreeChange> &changes) {
  if (changes.empty())
    return;

  std::cout << title << ":\n";

  for (const vcs::TreeChange &change : changes) {
    switch (change.type) {
    case vcs::ChangeType::Added:
      std::cout << "\tnew file:   ";
      break;
    case vcs::ChangeType::Modified:
      std::cout << "\tmodified:   ";
      break;
    case vcs::ChangeType::Deleted:
      std::cout << "\tdeleted:    ";
      break;
    }
    std::cout << change.path << "\n";
  }

  std::cout << "\n";
}

void cmd_status(const std::vector<std::string> &args) {
  if (!args.empty())
    throw std::runtime_error("usage: vcs status");

  vcs::StatusReport report = current_repo().status();

  if (report.branch)
    std::cout << "On branch " << *report.branch << "\n";
  else
    std::cout << "HEAD detached at " << report.head.value_or("").substr(0, 7)
              << "\n";

  if (!report.has_commits)
    std::cout << "\nNo commits yet\n";
  std::cout << "\n";

  print_changes("Changes to be committed", report.staged);
  print_changes("Changes not staged for commit", report.unstaged);

  if (!report.untracked.empty()) {
    std::cout << "Untracked files:\n";
    for (const std::string &path : report.untracked)
      std::cout << "\t" << path << "\n";
    std::cout << "\n";
  }

  if (report.staged.empty() && report.unstaged.empty() &&
      report.untracked.empty())
    std::cout << "nothing to commit, working tree clean\n";
}

void cmd_diff(const std::vector<std::string> &args) {
  bool staged = false;

  if (args.size() == 1 && (args[0] == "--staged" || args[0] == "--cached"))
    staged = true;
  else if (!args.empty())
    throw std::runtime_error("usage: vcs diff [--staged]");

  for (const vcs::FileChange &file : current_repo().diff(staged))
    std::cout << vcs::file_diff(file.path, file.old_content, file.new_content);
}

void cmd_branch(const std::vector<std::string> &args) {
  vcs::Repository repo = current_repo();

  if (args.size() == 1) {
    repo.create_branch(args[0]);
    return;
  }

  if (!args.empty())
    throw std::runtime_error("usage: vcs branch [<name>]");

  std::optional<std::string> current = repo.current_branch();

  if (!current)
    std::cout << "* (HEAD detached at "
              << repo.head_commit().value_or("").substr(0, 7) << ")\n";

  for (const std::string &name : repo.branches())
    std::cout << (name == current ? "* " : "  ") << name << "\n";
}

void cmd_checkout(const std::vector<std::string> &args) {
  vcs::Repository repo = current_repo();
  std::string target;
  bool new_branch = false;

  if (args.size() == 1 && !args[0].starts_with("-")) {
    target = args[0];
  } else if (args.size() == 2 && args[0] == "-b") {
    target = args[1];
    new_branch = true;
  } else {
    throw std::runtime_error("usage: vcs checkout [-b] <branch|commit>");
  }

  // the new branch starts at the current commit, so no files change
  if (new_branch)
    repo.create_branch(target);

  vcs::CheckoutResult result = repo.checkout(target);

  if (result.already_there)
    std::cout << "Already on '" << *result.branch << "'\n";
  else if (new_branch)
    std::cout << "Switched to a new branch '" << *result.branch << "'\n";
  else if (result.branch)
    std::cout << "Switched to branch '" << *result.branch << "'\n";
  else
    std::cout << "HEAD is now at " << result.commit.substr(0, 7) << "\n";
}

} // namespace

int main(int argc, char **argv) {
  if (argc < 2) {
    std::cerr << "usage: vcs <command> [args]\n";
    return 1;
  }

  std::string command = argv[1];
  std::vector<std::string> args(argv + 2, argv + argc);

  try {
    if (command == "init") {
      cmd_init(args);
    } else if (command == "hash-object") {
      cmd_hash_object(args);
    } else if (command == "cat-file") {
      cmd_cat_file(args);
    } else if (command == "config") {
      cmd_config(args);
    } else if (command == "add") {
      cmd_add(args);
    } else if (command == "commit") {
      cmd_commit(args);
    } else if (command == "log") {
      cmd_log(args);
    } else if (command == "status") {
      cmd_status(args);
    } else if (command == "diff") {
      cmd_diff(args);
    } else if (command == "branch") {
      cmd_branch(args);
    } else if (command == "checkout") {
      cmd_checkout(args);
    } else {
      std::cerr << "unknown command: " << command << "\n";
      return 1;
    }
  } catch (const std::exception &e) {
    std::cerr << "error: " << e.what() << "\n";
    return 1;
  }

  return 0;
}
