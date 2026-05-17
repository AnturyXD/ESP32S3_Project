"""日志初始化。

服务器当前处于骨架阶段，只配置控制台日志。后续如果落盘，需要加轮转，
避免服务器 journald 或 Docker json-file 日志无限增长。
"""

import logging


def setup_logging(level: str = "INFO") -> None:
    """初始化应用日志格式。

    不在日志中打印 API Key、Token、Password；后续业务模块也必须遵守这个规则。
    """

    logging.basicConfig(
        level=getattr(logging, level.upper(), logging.INFO),
        format="%(asctime)s %(levelname)s [%(name)s] %(message)s",
    )
