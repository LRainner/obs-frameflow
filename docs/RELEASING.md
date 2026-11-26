# 发布流程

本项目使用 [release-please](https://github.com/googleapis/release-please) 驱动，
配合 [Conventional Commits](https://www.conventionalcommits.org/) 自动生成
`CHANGELOG.md`、版本号、Git tag 和 GitHub Release。

## 版本号的唯一来源

`version.txt` 由 release-please 维护，CMake 从它读取版本号。
**不要手改** `version.txt`、`CHANGELOG.md` 或手动打 tag。

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
   该 PR 更新 `version.txt` 和 `CHANGELOG.md`。
3. 审阅这个 PR（版本号是否合理、changelog 分组是否正确）。
4. 合并 PR。release-please 随即：
   - 创建 tag `vX.Y.Z`
   - 创建**草稿** Release，正文取自 changelog 对应小节
5. 同一工作流继续执行：构建 Windows 产物 → 打包 → 生成 `SHA256SUMS.txt`
   → 通过 `gh release upload` 挂到该草稿 Release。
6. 确认附件与说明无误后，在 GitHub 上点 **Publish release**。

## 手工补发

如果需要在没有新提交的情况下重跑：

```
gh workflow run release.yml
```

或推送一个符合 semver 的 tag（例如 `v0.2.0`），但**不推荐**——会绕过
release-please 对 `version.txt` 的维护。

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
