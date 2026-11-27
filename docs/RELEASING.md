# 发布流程

本项目使用 [release-please](https://github.com/googleapis/release-please) 驱动，
配合 [Conventional Commits](https://www.conventionalcommits.org/) 自动生成
`docs/CHANGELOG.md`、版本号、Git tag 和 GitHub Release。

## 版本号的唯一来源

`cmake/version.txt` 由 release-please 维护，CMake 从它读取版本号。
**不要手改** `cmake/version.txt`、`docs/CHANGELOG.md` 或手动打 tag。

## 提交信息规范

| 前缀 | 含义 | 版本影响 |
|---|---|---|
| `feat:` | 新功能 | minor |
| `fix:` | 修复 | patch |
| `feat!:` / `fix!:` | 破坏性变更 | major |
| `perf:` | 性能 | patch |
| `refactor:` | 重构 | 无（会出现在 changelog） |
| `docs:` `chore:` `ci:` `build:` `test:` | 工程杂项 | 无（不进入 changelog） |

示例：

```
feat: add Maxine VSR upscaling stage
fix: release texrender when the input resolution changes
feat!: drop OBS 29 support
```

## 发版步骤

1. 功能分支合并进 `master`（建议 squash merge，保持线性历史）。
2. `release` 工作流运行，release-please 打开/更新一个 PR：
   `chore(master): release X.Y.Z`。
   该 PR 更新 `cmake/version.txt` 和 `docs/CHANGELOG.md`。
3. 审阅这个 PR（版本号是否合理、changelog 分组是否正确）。
4. 合并 PR。release-please 随即：
   - 创建 tag `vX.Y.Z`
   - 创建**草稿** Release，正文取自 changelog 对应小节
5. 同一工作流继续执行：构建 Windows 产物 → 打包 → 生成 `SHA256SUMS.txt`
   → 通过 `gh release upload` 挂到该草稿 Release。
6. 确认附件与说明无误后，在 GitHub 上点 **Publish release**。

## 补发附件

构建或上传失败时，不要重新 dispatch 工作流：release-please 已经把发布 PR 的
`autorelease: pending` 标签摘掉，重跑不会再有 `release_created=true`，
`build` 和 `publish` 会被直接跳过。

正确做法是重跑**原运行中失败的作业**，保留 release-please 的输出：

```
gh run list --workflow release.yml
gh run rerun <run-id> --failed
```

## 手动触发

`gh workflow run release.yml` 只会让 release-please 检查是否需要开/更新
发布 PR，不会构建二进制。

推送 tag **不会**触发任何工作流——tag 只由 release-please 创建，构建发生在
同一次 release 运行内。

## 产物结构

```
frameflow-<version>-win64.zip
└── frameflow/
    ├── bin/64bit/frameflow.dll
    ├── data/effects/frameflow.effect
    ├── data/locale/{en-US,zh-CN}.ini
    ├── LICENSE
    └── README.md
```

解压到 `C:\ProgramData\obs-studio\plugins\` 即可。
