#include "targetconfigparser.h"
#include <QCoreApplication>
#include <QFile>
#include <QJsonDocument>
#include <QTextStream>

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    QFile input;
    if (!input.open(stdin, QIODevice::ReadOnly))
        return 2;
    const auto document = QJsonDocument::fromJson(input.readAll());
    if (!document.isObject())
        return 1;
    ParsedTargetConfig parsed;
    QList<TargetDiagnostic> diagnostics;
    const bool valid = TargetConfigParser::parse(document.object(), &parsed, &diagnostics);
    QTextStream output(stderr);
    for (const auto& diagnostic : diagnostics)
        output << diagnostic.jsonPath << ": " << diagnostic.message << '\n';
    return valid ? 0 : 1;
}
