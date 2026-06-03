# 文档索引

本文档目录用于集中说明 `lightprobe` 的设计、分工、验证和展示材料。

## 推荐阅读顺序

1. `../README.md`
   - 项目入口、快速开始、CLI、验证矩阵和当前边界。

2. `project_structure.md`
   - 仓库目录结构和各模块职责。

3. `member_a_runtime_layout.md`
   - 成员 A 负责的 runtime、entry probe、return probe、shadow stack 和验证矩阵。

4. `verification_and_benchmark.md`
   - smoke 脚本、stress 脚本、benchmark 输出和 IFUNC 说明。

5. `member_b_task.md`
   - 成员 B controller 侧任务说明。

## 展示图示

```text
figures/lightprobe_architecture_cn.drawio
figures/lightprobe_architecture_cn.spec.yaml
figures/lightprobe_architecture_cn.arch.json
figures/lightprobe_architecture_cn.svg
figures/lightprobe_demo_flow.mmd
figures/lightprobe_demo_flow.dot
figures/lightprobe_demo_flow.svg
```

- `lightprobe_architecture_cn.svg`：中文框架图，推荐放在 README、报告或 PPT 中。
- `lightprobe_architecture_cn.drawio`：可用 draw.io 继续编辑的源文件。
- `lightprobe_architecture_cn.spec.yaml` / `.arch.json`：结构化图示源数据，便于后续维护。
- `lightprobe_demo_flow.*`：早期流程图源文件，保留用于对比和简化展示。
