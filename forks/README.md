# forks: the upstream trees the runner is built from

Every runner and module is built from five trees that are not ours alone: the recompiler (`dolrecomp`), the
emulator core (`recompcore`, checked out as `ModernGekko/vendor/dolphin`), the runtime (`moderngekko`), the module
template (one folder of RecompCore, checked out as `work/module-template`) and the recompiler as the runner's build
reads it (`dolrecomp-vendored`, the checkout inside the emulator core). Our changes to each are one commit on a
branch of our GitHub fork, on top of the upstream commit it started from, and each is a submodule here at
`forks/<tree>`:

| Submodule | Repository, branch | On top of |
|---|---|---|
| `forks/dolrecomp` | github.com/piconet-live/dolrecomp, `piconet` | [DolRecomp](https://github.com/ExpansionPak/DolRecomp) 71ce7f9 |
| `forks/dolrecomp-vendored` | github.com/piconet-live/dolrecomp, tag `piconet-vendored` | DolRecomp 71ce7f9 |
| `forks/moderngekko` | github.com/piconet-live/moderngekko, `piconet` | [ModernGekko](https://github.com/ExpansionPak/ModernGekko) a2e4e2b |
| `forks/recompcore` | github.com/piconet-live/recompcore, `piconet` | [RecompCore](https://github.com/ExpansionPak/RecompCore) cf8ceb8 |
| `forks/module-template` | github.com/piconet-live/recompcore, tag `piconet-module-template` | RecompCore cf8ceb8's folder `module-template`, without `generated/` (a root commit of the folder's files alone) |

| File | What it holds |
|---|---|
| `upstream.tsv` | Per tree: our fork's URL, the upstream base commit, our commit (the submodule's gitlink), its tree id (`git rev-parse HEAD^{tree}`), its place in the work folder, what its base leaves out |
| `apply.sh TREE DEST [SOURCE]` | Checks the tree out at DEST at the pinned commit, fetched from the submodule (or SOURCE), and fails unless the checkout has the pinned tree id. A checkout already at DEST is moved to the pin. `port/setup/trees.sh` runs it for every row |
| `check.sh` | The commit check: the pins are well formed and each row's commit is its submodule's |
| `check.sh --trees [WORK]` | A work folder's trees are the pinned ones (tree id, nothing uncommitted); every build runs it first |

## Changing a fork

Commit on the fork's branch (in the submodule, or a clone of the fork), then here: move the submodule to the new
commit (`git -C forks/<tree> checkout <commit>`, `git add forks/<tree>`) and set the row's `head` and `tree_id` to
the commit and its tree; `forks/check.sh` says whether they agree. `port/setup/trees.sh WORK` then moves a work
folder's trees to the new pins.

## What must never happen

**A build from a tree that is not the pinned one.** Before a build: `forks/check.sh --trees`. Only the tree id is
required: each tree must equal its row's `tree_id`.
