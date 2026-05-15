# Release Checklist

发版前逐项核对。全部打勾才能 tag。

---

## 1. 构建与测试

- [ ] `meson setup build && meson compile -C build` 在干净目录成功
- [ ] `meson test -C build` 全部通过（15 tests）
- [ ] `meson test -C build --benchmark` 通过，性能无 >10% 回归
- [ ] 零编译 warning（`-Werror` 开启）

## 2. 质量

- [ ] Valgrind 零泄漏：
  ```bash
  cd build && valgrind --leak-check=full --error-exitcode=1 ./tests/test_golden
  ```
- [ ] Vulkan smoke test通过（如有Vulkan设备）：`meson test -C build --suite integration`
- [ ] Golden 测试通过：`meson test -C build --suite golden`

## 3. API 与文档

- [ ] `include/flux/flux.h` 中所有公共函数/类型/枚举都有注释
- [ ] 无未使用的公共类型（如之前删除的 `flux_size`）
- [ ] `CHANGELOG.md` 已更新本次变动
- [ ] `docs/` 中无引用已删除文件的路径
- [ ] `README.md` 中的构建命令仍然正确

## 4. 版本号

- [ ] `meson.build` 中 `version` 已 bump
- [ ] `CHANGELOG.md` 中有对应版本号的小节
- [ ] 如有 `flux_version_string()`，返回值与 meson 一致

---

## 版本规则

| 版本 | 含义 | 何时打 tag |
|---|---|---|
| `0.2.x` | 继续迭代 | 每完成一个功能或修复即可发 |
| `1.0.0-rcN` | API 冻结候选 | 公共头文件连续 7 天无改动后 |
| `1.0.0` | 稳定版 | RC 试用无问题后 |

**API 冻结期**：宣布冻结后，禁止修改 `include/flux/*.h` 中的任何公共符号（函数名、参数、结构体字段、枚举值）。期间只修 bug、优化性能、更新文档。
