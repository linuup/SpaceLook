#pragma once

#include <QtGlobal>
#include <QString>

class PreviewLoadGuard
{
public:
    struct Token
    {
        quint64 generation = 0;
        QString path;
    };

    Token begin(const QString& path)
    {
        ++m_generation;
        return Token{m_generation, path};
    }

    Token observe(const QString& path) const
    {
        return Token{m_generation, path};
    }

    void cancel()
    {
        ++m_generation;
    }

    bool isCurrent(const Token& token, const QString& currentPath) const
    {
        return token.generation == m_generation && token.path == currentPath;
    }

    bool isCurrentGeneration(const Token& token) const
    {
        return token.generation == m_generation;
    }

private:
    quint64 m_generation = 0;
};
