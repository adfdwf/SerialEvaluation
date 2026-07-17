#ifndef TCPPORTPARSER_H
#define TCPPORTPARSER_H

#include "namespace.h"

#include <QString>
#include <QVector>

BEGIN_NAMESPACE_CIQTEK

/** 解析批量 TCP 端口输入的结果。 */
struct TcpPortParseResult {
    QVector<quint16> ports; ///< 按首次出现顺序排列的有效、不重复端口。
    int invalidCount = 0;   ///< 非空但无效的输入项数量。
    int duplicateCount = 0; ///< 重复有效端口数量。
};

/** 将逗号、分号、空白和换行分隔的文本解析为 TCP 端口列表。 */
TcpPortParseResult parseTcpPortList(const QString &text);

END_NAMESPACE_CIQTEK

#endif // TCPPORTPARSER_H
