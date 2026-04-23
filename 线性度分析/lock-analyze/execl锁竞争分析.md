# execl 容器线性度测试 —— 锁竞争深度分析

> 基于 Linux 6.6.0-132 (openEuler, AArch64) 内核源码分析 execl 测试过程中涉及的锁，
> 重点解析 `i_mmap_rwsem` 的触发路径，并结合静态编译实验结果进行归因。

---

## 一、execl 测试的内核调用链全景

每次 `execl()` 调用在内核中的完整路径：

```
execve() 系统调用
  └─ do_execveat_common()                     [fs/exec.c]
      ├─ do_open_execat()                     ← 打开可执行文件, 涉及 VFS 锁
      ├─ bprm_mm_init()                       ← 创建新 mm_struct, 设置临时栈 VMA
      ├─ copy_strings()                       ← 拷贝参数/环境变量到新栈页
      ├─ begin_new_exec()                     [fs/exec.c:1247]
      │   ├─ de_thread()                      ← tasklist_lock (write_lock_irq)
      │   ├─ unshare_files()                  ← files_struct 锁
      │   ├─ set_mm_exe_file()                ← mm->exe_file 更新
      │   ├─ exec_mmap(bprm->mm)              [fs/exec.c:980] ★ 核心瓶颈
      │   │   ├─ exec_update_lock             ← 写信号量 (signal->exec_update_lock)
      │   │   ├─ mmap_read_lock(old_mm)       ← 旧 mm 的读锁
      │   │   ├─ activate_mm()                ← 切换地址空间
      │   │   └─ mmput(old_mm)                ← 释放旧 mm ★★★
      │   │       └─ exit_mmap()              ← 遍历所有 VMA 释放
      │   │           ├─ unlink_file_vma()     ← i_mmap_rwsem 写锁 ★
      │   │           └─ unmap_vmas()          ← 解除页表映射
      │   ├─ unshare_sighand()                ← tasklist_lock
      │   ├─ do_close_on_exec()               ← files->file_lock
      │   └─ commit_creds()                   ← 凭证切换
      └─ search_binary_handler()
          └─ load_elf_binary()                [fs/binfmt_elf.c:829]
              ├─ begin_new_exec()             ← (同上)
              ├─ setup_arg_pages()            ← mmap_write_lock, mprotect_fixup
              ├─ elf_map() × N               ← do_mmap() ★
              │   └─ mmap_region()
              │       └─ vma_link()            ← i_mmap_rwsem 写锁 ★
              └─ [动态链接时] load_elf_interp() ← 加载 ld.so, 额外 mmap
```

---

## 二、execl 测试涉及的全部锁清单

按 execve 调用链顺序排列，标注竞争级别（🔴高 🟡中 🟢低）：

### 2.1 VFS / 文件系统层

| 锁 | 类型 | 持锁点 | 竞争级别 | 说明 |
|---|---|---|:---:|---|
| `inode->i_rwsem` | rw_semaphore | `do_open_execat()` → `do_filp_open()` | 🟡 | 打开可执行文件的 inode 锁 |
| `dentry->d_lock` | spinlock | 路径解析 (dcache lookup) | 🟡 | OverlayFS 多层查找放大 |
| `superblock->s_vfs_rename_mutex` | mutex | OverlayFS 目录操作 | 🟢 | 仅目录修改时 |
| `deny_write_access` | atomic | `do_open_execat()` | 🟢 | `i_writecount` 原子操作 |

### 2.2 内存管理 (MM) 层 ★ 主要瓶颈区

| 锁 | 类型 | 持锁点 | 竞争级别 | 说明 |
|---|---|---|:---:|---|
| **`mm->mmap_lock`** | rw_semaphore | `exec_mmap()`, `setup_arg_pages()`, `mmap_region()` | 🔴 | 进程地址空间操作的主锁 |
| **`mapping->i_mmap_rwsem`** | rw_semaphore | `vma_link_file()`, `unlink_file_vma()`, `vma_prepare()` | 🔴🔴 | **同一文件的所有映射者共享**，详见第三节 |
| `zone->lock` | spinlock | `get_page_from_freelist()` | 🟡 | Buddy 分配器的 Zone 锁 |
| `anon_vma->rwsem` | rw_semaphore | `vma_prepare()`, rmap 操作 | 🟡 | 匿名 VMA 的 reverse mapping 锁 |
| `page_table_lock` / `pte_lock` | spinlock | `handle_mm_fault()`, `unmap_page_range()` | 🟡 | 页表修改的细粒度锁 |

### 2.3 进程管理层

| 锁 | 类型 | 持锁点 | 竞争级别 | 说明 |
|---|---|---|:---:|---|
| `tasklist_lock` | rwlock | `de_thread()`, `unshare_sighand()` | 🟡 | 全局任务列表锁 |
| `signal->exec_update_lock` | rw_semaphore | `exec_mmap()` | 🟢 | 进程内部信号量 |
| `sighand->siglock` | spinlock | `posix_cpu_timers_exit()` | 🟢 | 信号处理锁 |
| `signal->cred_guard_mutex` | mutex | 凭证切换 | 🟢 | 进程凭证保护 |

### 2.4 Cgroup / MemCG 层

| 锁 | 类型 | 持锁点 | 竞争级别 | 说明 |
|---|---|---|:---:|---|
| `memcg->move_lock` | spinlock | `mem_cgroup_uncharge()` | 🟡 | memcg 记账解除 |
| `memcg internal locks` | per-cpu / spinlock | 页面计数更新 | 🟡 | 高并发下退化为全局竞争 |

---

## 三、i_mmap_rwsem 深度解析

### 3.1 数据结构定义

`i_mmap_rwsem` 是 `struct address_space` 中的读写信号量，定义于 `include/linux/fs.h:514`：

```c
struct address_space {
    struct inode        *host;         // 所属 inode
    struct xarray        i_pages;      // page cache
    struct rb_root_cached i_mmap;      // ★ VMA interval tree（所有映射该文件的 VMA）
    struct rw_semaphore   i_mmap_rwsem; // ★ 保护 i_mmap 和 i_mmap_writable
    atomic_t              i_mmap_writable; // VM_SHARED 映射计数
    // ...
};
```

**关键语义**：`i_mmap_rwsem` 保护一个 **interval tree**（`i_mmap`），这棵树记录了**所有映射到该文件的 VMA**。

> 这意味着：**映射同一个文件的所有进程/容器，共享同一把 `i_mmap_rwsem` 锁！**

### 3.2 在 execve 路径中触发 i_mmap_rwsem 的三个关键点

#### 触发点 ①：创建 file-backed VMA —— `vma_link_file()`

当 `load_elf_binary()` 通过 `elf_map()` → `do_mmap()` → `mmap_region()` → `vma_link()` 将 ELF 段映射到进程地址空间时：

```c
// mm/mmap.c:440
static void vma_link_file(struct vm_area_struct *vma)
{
    struct file *file = vma->vm_file;
    struct address_space *mapping;

    if (file) {
        mapping = file->f_mapping;
        i_mmap_lock_write(mapping);            // ★ 获取写锁
        __vma_link_file(vma, mapping);         // 插入 interval tree
        i_mmap_unlock_write(mapping);          // ★ 释放写锁
    }
}
```

**execl 每次迭代触发次数**：
- 动态编译：4-8 次（ELF text/data + ld.so text/data + 用户态 libc/libm 各段 mmap）
- 静态编译：2-3 次（仅 ELF 自身的 text/data/bss 段）

#### 触发点 ②：销毁 file-backed VMA —— `unlink_file_vma()`

当 `exec_mmap()` → `mmput()` → `exit_mmap()` 释放旧进程地址空间时，每个 file-backed VMA 都需要从文件的 interval tree 中移除：

```c
// mm/mmap.c:126
void unlink_file_vma(struct vm_area_struct *vma)
{
    struct file *file = vma->vm_file;

    if (file) {
        struct address_space *mapping = file->f_mapping;
        i_mmap_lock_write(mapping);                    // ★ 获取写锁
        __remove_shared_vm_struct(vma, mapping);       // 从 interval tree 移除
        i_mmap_unlock_write(mapping);                  // ★ 释放写锁
    }
}
```

**批量优化版本**（同一文件的多个 VMA 可以批量处理）：

```c
// mm/mmap.c:143
static void unlink_file_vma_batch_process(struct unlink_vma_file_batch *vb)
{
    struct address_space *mapping;
    mapping = vb->vmas[0]->vm_file->f_mapping;
    i_mmap_lock_write(mapping);              // ★ 一次写锁
    for (i = 0; i < vb->count; i++) {
        __remove_shared_vm_struct(vb->vmas[i], mapping);
    }
    i_mmap_unlock_write(mapping);            // ★ 一次释放
}
```

#### 触发点 ③：VMA 合并/修改 —— `vma_prepare()` / `vma_complete()`

当内核尝试合并相邻 VMA（`vma_merge`）、扩展 VMA（`vma_expand`）、或分割 VMA（`split_vma`）时：

```c
// mm/mmap.c:512
static inline void vma_prepare(struct vma_prepare *vp)
{
    if (vp->file) {
        i_mmap_lock_write(vp->mapping);      // ★ 获取写锁
        // 从 interval tree 移除旧 VMA
        vma_interval_tree_remove(vp->vma, &vp->mapping->i_mmap);
    }
}

// mm/mmap.c:559
static inline void vma_complete(struct vma_prepare *vp, ...)
{
    if (vp->file) {
        // 重新插入修改后的 VMA
        vma_interval_tree_insert(vp->vma, &vp->mapping->i_mmap);
        // ...
        i_mmap_unlock_write(vp->mapping);    // ★ 释放写锁
    }
}
```

### 3.3 i_mmap_rwsem 在容器并发下的竞争模型

```
容器 1 的 execl 进程:                      容器 N 的 execl 进程:
  │                                          │
  ├─ load_elf_binary()                       ├─ load_elf_binary()
  │   └─ elf_map("execl") ──────┐            │   └─ elf_map("execl") ──────┐
  │       └─ vma_link_file() ───┤            │       └─ vma_link_file() ───┤
  │                             │            │                             │
  │                             ▼            │                             ▼
  │                    ┌─────────────────┐   │                    各容器独立的
  │                    │ execl 文件的     │   │                    execl 文件
  │                    │ address_space   │   │                    address_space
  │                    │ .i_mmap_rwsem  │   │                    .i_mmap_rwsem
  │                    └────────┬────────┘   │                    (无竞争)
  │                             │            │
  └─ [动态链接] 用户态 ld.so     │            └─ [动态链接] 用户态 ld.so
      ├─ mmap(libc.so) ────────┐│                ├─ mmap(libc.so) ────────┐
      ├─ mmap(libm.so) ────────┤│                ├─ mmap(libm.so) ────────┤
      └─ mmap(其他.so) ────────┘│                └─ mmap(其他.so) ────────┘
                                ▼                                          │
                    ┌──────────────────────────────────────────────────────┐│
                    │ libc.so 的 address_space.i_mmap_rwsem               ││
                    │ ★★★ 所有容器共享同一个锁！全局写锁串行化！ ★★★       │←┘
                    └──────────────────────────────────────────────────────┘
```

**核心矛盾**：
- `execl` 可执行文件：如果每个容器独立挂载了自己的副本 → **各自独立的 inode/address_space** → `i_mmap_rwsem` 无竞争
- `libc.so` 等共享库：所有容器镜像的 OverlayFS lower layer **指向同一个 inode** → **共享同一个 `i_mmap_rwsem`** → 高并发写锁串行化！

---

## 四、静态编译减少了哪个过程的锁竞争？

### 4.1 实验数据回顾

| 场景 | 编译方式 | execl 跑分 | 相对基线 |
|---|---|---|---|
| B | 动态 | 150 | 2.5% |
| D | **静态** | 1700-1900 | **28-32%** |

静态编译带来 **~10 倍** 跑分提升。

### 4.2 静态编译消除的锁竞争

`★ Insight ─────────────────────────────────────`
静态编译的核心收益 **不是减少了 execve 内核态的锁竞争**（eBPF 数据证明内核侧耗时 B ≈ D），
而是 **彻底消除了用户态动态链接过程中对共享库 i_mmap_rwsem 的跨容器全局锁竞争**。
`─────────────────────────────────────────────────`

具体消除的锁竞争环节：

#### ① 消除共享库的 `i_mmap_rwsem` 写锁竞争 (最关键)

动态编译时，每次 `execl` 返回用户态后，`ld.so` 需要：

```
mmap(libc.so .text段) → mmap_region() → vma_link_file()
    → i_mmap_lock_write(&libc_inode->i_mapping->i_mmap_rwsem)  // ★ 全局写锁!
    → vma_interval_tree_insert()
    → i_mmap_unlock_write()

mmap(libc.so .rodata段) → 同上, 再次获取写锁
mmap(libc.so .data段)   → 同上
mmap(libm.so .text段)   → 同上 (libm.so 的 i_mmap_rwsem)
...
```

每次 `execl` 迭代，动态链接需要对 **3-5 个共享库文件** 各执行 **2-4 次** `i_mmap_rwsem` 写锁，总计 **6-20 次全局写锁竞争**。

40 个容器同时以每秒数千次的频率执行 `execl`，意味着 `libc.so` 的 `i_mmap_rwsem` 上每秒有 **数万级** 的写锁竞争。

**静态编译后**：`ld.so` 不再启动，无需 `mmap` 任何共享库 → **这些写锁竞争全部归零**。

#### ② 消除共享库的 OverlayFS inode 锁竞争

动态链接需要 `open()` 共享库 → 穿越 OverlayFS 的 `ovl_lookup()` → 获取底层 `inode->i_rwsem`。

所有容器的 OverlayFS lower layer 指向宿主机同一个 `libc.so` inode → **跨容器 inode 锁串行化**。

**静态编译后**：无需打开任何共享库 → **inode 锁竞争归零**。

#### ③ 减少 VMA 数量 → 降低 exit_mmap 的 i_mmap_rwsem 竞争

| 编译方式 | VMA 数量 | unlink_file_vma 调用次数 | i_mmap_rwsem 写锁次数 |
|---|---|---|---|
| 动态 | 20-30+ | 10-15 次 (file-backed VMA) | 10-15 次 |
| 静态 | 5-8 | 2-3 次 | 2-3 次 |

在下一次 `execl` 调用时，`exec_mmap()` → `mmput()` → `exit_mmap()` 需要遍历**上一次迭代创建的所有 VMA**，对每个 file-backed VMA 调用 `unlink_file_vma()` 获取其对应文件的 `i_mmap_rwsem` 写锁。

**静态编译**: VMA 数量减少 3-5 倍 → exit_mmap 中的 i_mmap_rwsem 竞争相应减少。

#### ④ 减少缺页异常 → 降低 zone->lock 和页表锁竞争

动态链接需要多个 .so 的每页首次访问触发 page fault → `handle_mm_fault()` → `alloc_pages()` → `zone->lock`。

静态编译后仅自身 ELF 触发 page fault，分配次数大幅减少。

### 4.3 锁竞争消除量化

```
单次 execl 迭代中 i_mmap_rwsem 写锁获取次数：

动态编译：
  创建阶段 (load_elf_binary + ld.so mmap):
    execl 二进制本身:  2-3 次
    ld.so 二进制:      2 次
    libc.so:           3-4 次  ← ★ 全局竞争（所有容器共享）
    libm.so:           2-3 次  ← ★ 全局竞争
    其他 .so:          2-4 次  ← ★ 全局竞争
  销毁阶段 (下一次 exec_mmap → exit_mmap):
    全部 file-backed VMA: 10-15 次（含共享库 VMA → 再次全局竞争）
  ─────────────────
  总计: ~20-30 次/迭代, 其中 ~15-20 次在全局共享的锁上

静态编译：
  创建阶段 (load_elf_binary):
    execl 二进制本身:  2-3 次  ← 独立副本, 无竞争
  销毁阶段:
    file-backed VMA:   2-3 次  ← 独立副本, 无竞争
  ─────────────────
  总计: ~4-6 次/迭代, 且全部在独立 inode 的锁上, 零跨容器竞争
```

---

## 五、总结：锁竞争分层归因

```
execl 容器线性度问题的锁竞争分层：

┌─────────────────────────────────────────────────────────────────┐
│ 第一层：用户态动态链接引入的全局 i_mmap_rwsem 竞争               │
│   ├─ libc.so/libm.so 等共享库 mmap → vma_link_file()          │
│   │   → 所有容器写锁同一个 address_space.i_mmap_rwsem          │
│   ├─ 共享库 open → OverlayFS inode->i_rwsem 串行化             │
│   └─ ★ 静态编译可 100% 消除此层竞争，跑分提升 10x              │
├─────────────────────────────────────────────────────────────────┤
│ 第二层：exec_mmap 的 VMA 拆卸竞争                               │
│   ├─ exit_mmap → unlink_file_vma → i_mmap_rwsem 写锁           │
│   ├─ 动态编译 VMA 多 → 更多次写锁获取                           │
│   └─ ★ 静态编译减少 VMA 到 1/4，间接缓解此层                    │
├─────────────────────────────────────────────────────────────────┤
│ 第三层：mm->mmap_lock 进程地址空间锁                             │
│   ├─ setup_arg_pages、mmap_region 等持写锁                      │
│   └─ 进程级锁，不跨容器竞争，但高频 exec 下仍有压力              │
├─────────────────────────────────────────────────────────────────┤
│ 第四层：zone->lock / page_table_lock 内存分配竞争                │
│   ├─ handle_mm_fault → alloc_pages → zone 自旋锁               │
│   └─ 静态编译仍存在（是剩余 70% 损耗的主因之一）                 │
├─────────────────────────────────────────────────────────────────┤
│ 第五层：memcg 记账锁                                            │
│   ├─ mem_cgroup_uncharge 在 VMA 多 + 有 memory 限制时加重        │
│   └─ 静态编译 + 无 memory 限制时可忽略                           │
└─────────────────────────────────────────────────────────────────┘
```

---

## 六、i_mmap_rwsem 竞争的根因与优化方向

### 6.1 根因

`i_mmap_rwsem` 是 **per-file（per-inode）** 粒度的锁。当多个进程/容器映射同一个文件时，所有 `mmap`/`munmap` 操作都要竞争这把锁。

在容器场景下，OverlayFS 的 lower layer 使所有容器 **共享底层的 libc.so inode**，把本应分散的 per-file 锁汇聚成了事实上的 **全局锁**。

### 6.2 优化方向

| 优先级 | 方案 | 预期效果 | 复杂度 |
|:---:|---|---|---|
| P0 | **静态编译 UnixBench** | 消除共享库 i_mmap_rwsem 竞争，跑分提升 10x | 低 |
| P0 | 容器内独立 /lib64 副本 (bind mount) | 拆分共享库 inode，每容器独立 i_mmap_rwsem | 中 |
| P1 | tmpfs 挂载共享库目录 | 绕过 OverlayFS，减少 inode 锁层级 | 低 |
| P2 | 内核升级启用 per-VMA lock | 减少 mmap_lock 持锁范围 | 高 |
| P2 | 内核补丁：批量 vma_link 优化 | 减少 i_mmap_rwsem 单次持锁时间 | 高 |

---

## 七、附录：OverlayFS 与 Lower Layer 背景知识

### 7.1 什么是 OverlayFS？

OverlayFS 是 Linux 内核内置的 **联合文件系统**（Union Filesystem），也是 Docker/containerd 默认使用的容器存储驱动。它的核心思想是将多个目录 **"叠加"** 在一起，上层目录覆盖下层目录，形成一个统一的合并视图。

```
用户 / 容器看到的文件系统（merged 视图）
        ┌────────────────────────┐
        │  /usr/bin/execl        │  ← 来自 upper（容器自己写入的）
        │  /lib64/libc.so.6     │  ← 来自 lower（基础镜像只读层）
        │  /etc/resolv.conf     │  ← 来自 upper（容器运行时修改的）
        │  /lib64/libm.so.6     │  ← 来自 lower
        └────────────────────────┘
                   │
          OverlayFS 合并
                   │
    ┌──────────────┼──────────────┐
    │              │              │
    ▼              ▼              ▼
┌────────┐  ┌───────────┐  ┌──────────┐
│ upper  │  │  lower    │  │  work    │
│(可读写) │  │ (只读)    │  │ (内部用) │
└────────┘  └───────────┘  └──────────┘
容器私有      镜像层共享       临时目录
```

OverlayFS 挂载时需要指定三个目录：

- **lowerdir**（下层，只读）：存放基础镜像内容，可以有多层
- **upperdir**（上层，可读写）：存放容器运行期间的修改（新文件、修改的文件）
- **workdir**（工作目录）：OverlayFS 内部使用的临时目录

### 7.2 什么是 Lower Layer？

**Lower layer（下层）** 是 OverlayFS 中 **只读** 的底层目录，通常包含容器镜像的内容。

```bash
# 典型的 Docker OverlayFS 挂载:
mount -t overlay overlay \
  -o lowerdir=/var/lib/docker/overlay2/<layer-hash>/diff,  \  # ← Lower Layer
     upperdir=/var/lib/docker/overlay2/<container-id>/diff, \  # ← Upper Layer
     workdir=/var/lib/docker/overlay2/<container-id>/work    \
  /var/lib/docker/overlay2/<container-id>/merged               # ← 合并视图
```

**核心特性**：

| 特性 | 说明 |
|---|---|
| **只读** | Lower layer 中的文件不可修改，容器写入时触发 Copy-on-Write（复制到 upper） |
| **共享** | 同一个镜像创建的所有容器 **共享同一个 lower layer 目录** |
| **多层叠加** | Docker 镜像由多个层组成，每层是一个 lower directory，从下到上叠加 |

### 7.3 Lower Layer 共享如何导致锁竞争

这是理解本文分析结论的关键：

```
宿主机文件系统:  /var/lib/docker/overlay2/abc123/diff/lib64/libc.so.6
                                                          │
                                                    inode #12345
                                                    └─ i_data.i_mmap_rwsem  ← 唯一实例
                                                          │
                 ┌──────────────────┬──────────────────────┼──────────────┐
                 │                  │                      │              │
           容器 1 的 merged    容器 2 的 merged       容器 3 的 merged  ...
           /lib64/libc.so.6   /lib64/libc.so.6      /lib64/libc.so.6
                 │                  │                      │
            ovl_open()         ovl_open()             ovl_open()
                 │                  │                      │
                 └──────────────────┴──────────────────────┘
                              最终都指向
                         宿主机的 inode #12345
```

**关键点**：
1. 40 个容器都使用同一个基础镜像 → 它们的 lower layer **是同一个目录**
2. 容器内的 `/lib64/libc.so.6` 实际上是 lower layer 中的文件 → **指向宿主机同一个 inode**
3. 所有 `mmap(libc.so)` 操作都要获取 **同一个 `inode->i_data.i_mmap_rwsem`** 的写锁
4. 结果：40 个容器高频并发 `execl` → 动态链接器 mmap 共享库 → **万级/秒的全局写锁竞争**

**对比**：如果每个容器有 **物理上独立的 libc.so 副本**（不同的 inode），那么每个容器操作的是各自独立的 `i_mmap_rwsem`，跨容器零竞争。这也解释了为什么静态编译（完全不需要 mmap 共享库）能带来 10 倍跑分提升。
