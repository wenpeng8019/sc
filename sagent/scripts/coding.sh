#!/bin/sh
# coding agent 编排模板（OUTLINE §3：编排在脚本，sca 只做原子操作）
# 用法：先编辑 .sagent/task/goal.md 与 plan.md（队列项 "- [ ] 目标"），然后：
#   sagent/scripts/coding.sh [--llm 段名]
# 退出码：0=队列消费完（task 完成）；43=预算耗尽；其余=loop 失败需人工介入。
#
# 终止条件三态（OUTLINE §3）：最终验证通过（队列空即全部 loop 验证过）|
# loop 预算耗尽（config [loop] budget）| 人工中断（Ctrl-C）。

SCA="${SCA:-sca}"

while :; do
    "$SCA" next "$@"
    rc=$?
    case $rc in
        0)  continue ;;                         # 本项完成，消费下一项
        10) echo "sca: 本 loop 验证未过，重试（预算兜底）"; continue ;;
        42) echo "sca: 队列消费完毕——task 完成"; exit 0 ;;
        43) echo "sca: 预算耗尽，停止"; exit 43 ;;
        *)  echo "sca: loop 失败（rc=$rc），人工介入"; exit $rc ;;
    esac
done
