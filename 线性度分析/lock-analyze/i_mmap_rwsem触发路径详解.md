# i_mmap_rwsem 在 execl 测试中的触发路径详解

> 基于 Linux 6.6.0-132 (openEuler, AArch64) 内核源码，
> 精确追踪 `i_mmap_rwsem` 在 execve 系统调用全流程中的每一次获取和释放。

---

## 一、i_mmap_rwsem 是什么？

### 1.1 定义

`i_mmap_rwsem` 是 **`struct address_space`** 的成员，类型为 `struct rw_semaphore`。

```c
// include/linux/fs.h:496-514
struct address_space {
    struct inode        *host;
    struct rb_root_cached i_mmap;       // VMA interval tree
    struct rw_semaphore   i_mmap_rwsem; // ★ 保护 i_mmap 树
    atomic_t              i_mmap_writable;
    // ...
};
```

每个 `struct inode` 内嵌一个 `struct address_space i_data`，即 `inode->i_data.i_mmap_rwsem`。

### 1.2 作用

保护 `i_mmap` **interval tree** —— 记录所有通过 `mmap()` 映射到该文件的 VMA。

操作该树需要持锁：
- **读锁**：遍历映射（如 `rmap` 反向映射查找、`truncate` 通知）
- **写锁**：插入/删除 VMA（如 `mmap`/`munmap`/`exec`）

### 1.3 锁粒度

**per-inode** 粒度。同一文件的所有映射者共享一把锁。

### 1.4 内联包装函数

```c
// include/linux/fs.h:544-571
static inline void i_mmap_lock_write(struct address_space *mapping)
{
    down_write(&mapping->i_mmap_rwsem);
}

static inline void i_mmap_unlock_write(struct address_space *mapping)
{
    up_write(&mapping->i_mmap_rwsem);
}

static inline void i_mmap_lock_read(struct address_space *mapping)
{
    down_read(&mapping->i_mmap_rwsem);
}

static inline void i_mmap_unlock_read(struct address_space *mapping)
{
    up_read(&mapping->i_mmap_rwsem);
}
```

---

## 二、execve 路径中 i_mmap_rwsem 的触发时序

以一次 `execl` 调用为例，按时间顺序列出所有 `i_mmap_rwsem` 持锁操作：

### 阶段 A：旧进程清理（exec_mmap → mmput → exit_mmap）

当 `exec_mmap()` 调用 `mmput(old_mm)` 释放旧地址空间时，`exit_mmap()` 遍历旧 mm 的所有 VMA：

```
exec_mmap() [fs/exec.c:980]
  └─ mmput(old_mm)
      └─ __mmput()
          └─ exit_mmap(old_mm)  [mm/mmap.c:3407]
              ├─ 遍历所有 VMA，对每个 file-backed VMA 调用:
              │
              │   unlink_file_vma_batch_add(vma) [mm/mmap.c:159]
              │     └─ 当 batch 满或文件切换时:
              │         unlink_file_vma_batch_process()  [mm/mmap.c:143]
              │           ├─ i_mmap_lock_write(mapping)     ★ 写锁
              │           ├─ __remove_shared_vm_struct() × N  (从 interval tree 移除)
              │           └─ i_mmap_unlock_write(mapping)   ★ 释放
              │
              ├─ 或逐个调用:
              │   unlink_file_vma(vma) [mm/mmap.c:126]
              │     ├─ i_mmap_lock_write(mapping)           ★ 写锁
              │     ├─ __remove_shared_vm_struct(vma, mapping)
              │     └─ i_mmap_unlock_write(mapping)         ★ 释放
              │
              └─ unmap_vmas() → free_pgtables()  (页表释放，不涉及 i_mmap_rwsem)
```

**动态编译时此阶段的 i_mmap_rwsem 写锁次数**（上一次迭代遗留的 VMA）：

| file-backed VMA 来源 | 数量 | 写锁对象 |
|---|---|---|
| execl 二进制 text/data | 2-3 | execl 文件的 i_mmap_rwsem |
| ld.so text/data | 2 | ld.so 的 i_mmap_rwsem |
| libc.so text/rodata/data | 3-4 | **libc.so 的 i_mmap_rwsem** ★ |
| libm.so text/data | 2-3 | **libm.so 的 i_mmap_rwsem** ★ |
| 其他 .so | 2-4 | 全局共享 ★ |
| **合计** | **~11-16** | |

### 阶段 B：ELF 加载（load_elf_binary）

`load_elf_binary()` 通过 `elf_map()` 映射新 ELF 的各段：

```
load_elf_binary() [fs/binfmt_elf.c:829]
  ├─ begin_new_exec()  ← 包含阶段 A 的 exec_mmap
  ├─ setup_arg_pages()
  │     └─ mmap_write_lock(mm)
  │     └─ mprotect_fixup() → vma_prepare() / vma_complete()
  │           ├─ i_mmap_lock_write(mapping)     ★ (如果栈 vma 有 vm_file)
  │           └─ i_mmap_unlock_write(mapping)   ★ (通常栈是匿名的，不触发)
  │
  ├─ elf_map(bprm->file, ...) × N段
  │     └─ vm_mmap() → do_mmap() → mmap_region()
  │           └─ vma_link()  [mm/mmap.c:453]
  │                 └─ vma_link_file()  [mm/mmap.c:440]
  │                       ├─ i_mmap_lock_write(mapping)     ★ 写锁
  │                       ├─ __vma_link_file()  (interval tree insert)
  │                       └─ i_mmap_unlock_write(mapping)   ★ 释放
  │
  └─ [有动态链接器时] load_elf_interp()
        └─ elf_map(interpreter_file, ...) × N段
              └─ ...同上...  i_mmap_rwsem 写锁
```

**此阶段的 i_mmap_rwsem 写锁次数**：

| 映射对象 | 段数 | 写锁对象 |
|---|---|---|
| execl ELF (text, data, bss) | 2-3 | execl 的 i_mmap_rwsem |
| ld.so interpreter (text, data) | 2 | ld.so 的 i_mmap_rwsem |
| **合计** | **4-5** | |

### 阶段 C：用户态动态链接（仅动态编译）

内核态 `execve()` 返回后，控制权交给 `ld.so` 动态链接器，在用户态通过 `mmap()` 系统调用映射共享库：

```
用户态 ld-linux-aarch64.so:
  ├─ openat("/lib64/libc.so.6")  → 进入内核 → OverlayFS inode 锁
  ├─ mmap(libc .text)  → 进入内核 → mmap_region() → vma_link_file()
  │     ├─ i_mmap_lock_write(libc_mapping)     ★★ 全局竞争写锁!
  │     └─ i_mmap_unlock_write(libc_mapping)   ★★
  ├─ mmap(libc .rodata) → 同上
  ├─ mmap(libc .data)   → 同上
  ├─ mmap(libc .bss)    → 匿名映射，无 i_mmap_rwsem
  ├─ openat("/lib64/libm.so.6")  → OverlayFS inode 锁
  ├─ mmap(libm .text)  → i_mmap_lock_write(libm_mapping)  ★★
  ├─ mmap(libm .data)  → 同上
  └─ ... 其他依赖库同理
```

**此阶段的 i_mmap_rwsem 写锁次数**：

| 共享库 | mmap 次数 | 写锁对象 | 竞争性 |
|---|---|---|---|
| libc.so | 3-4 | **全局共享** | ★★★ |
| libm.so | 2-3 | **全局共享** | ★★★ |
| 其他 .so | 2-4 | **全局共享** | ★★★ |
| **合计** | **7-11** | | **所有容器串行化** |

---

## 三、单次 execl 迭代的 i_mmap_rwsem 写锁总计

| 阶段 | 动态编译 | 静态编译 | 全局竞争写锁数 |
|---|---|---|---|
| A: 旧 VMA 清理 (exit_mmap) | 11-16 次 | 2-3 次 | 动态: 7-11 / 静态: 0 |
| B: ELF 加载 (load_elf_binary) | 4-5 次 | 2-3 次 | 0（各自 inode） |
| C: 用户态动态链接 (ld.so mmap) | 7-11 次 | 0 次 | 动态: **7-11** / 静态: 0 |
| **总计** | **22-32 次** | **4-6 次** | 动态: **14-22** / 静态: **0** |

> **结论**：静态编译将每次 execl 迭代的 `i_mmap_rwsem` 写锁从 22-32 次降至 4-6 次，
> 更关键的是将 **跨容器全局竞争的写锁** 从 14-22 次降至 **0 次**。

---

## 四、OverlayFS 对 i_mmap_rwsem 竞争的放大效应

所有容器使用相同的基础镜像，OverlayFS 的 lower layer 指向宿主机同一份文件：

```
宿主机文件系统:
  /var/lib/docker/overlay2/lower/lib64/libc.so.6
    └─ inode #12345
        └─ i_data.i_mmap_rwsem  ← ★ 唯一实例，所有容器共享

容器 1: mmap("/lib64/libc.so.6") → ovl_mmap() → 最终操作 inode #12345 的 i_mmap
容器 2: mmap("/lib64/libc.so.6") → ovl_mmap() → 最终操作 inode #12345 的 i_mmap
...
容器 N: mmap("/lib64/libc.so.6") → ovl_mmap() → 最终操作 inode #12345 的 i_mmap
```

40 个容器 × 每秒 ~150 次 execl × 每次 ~3 次 libc mmap = **~18000 次/秒的 i_mmap_rwsem 写锁竞争**，全部串行化在同一把锁上。

这就是为什么静态编译（消除共享库 mmap）能带来 10 倍跑分提升的根本原因。
