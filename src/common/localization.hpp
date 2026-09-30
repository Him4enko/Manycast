#pragma once

#include <obs-module.h>

#include <QString>

namespace manycast {

inline QString text(const char *key)
{
	return QString::fromUtf8(obs_module_text(key));
}

inline QString text(const char *key, const QString &arg1)
{
	return text(key).arg(arg1);
}

inline QString text(const char *key, const QString &arg1, const QString &arg2)
{
	return text(key).arg(arg1, arg2);
}

} // namespace manycast
