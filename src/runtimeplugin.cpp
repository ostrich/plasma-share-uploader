#include "sharejob.h"

#include <KPluginFactory>
#include <Purpose/PluginBase>

class RuntimePlugin final : public Purpose::PluginBase
{
    Q_OBJECT
public:
    explicit RuntimePlugin(QObject *parent, const KPluginMetaData &metaData, const QVariantList &args)
        : Purpose::PluginBase(parent)
    {
        Q_UNUSED(metaData)
        Q_UNUSED(args)
    }

    Purpose::Job *createJob() const override
    {
        return new ShareJob(QByteArray{});
    }
};

K_PLUGIN_CLASS_WITH_JSON(RuntimePlugin, "runtimeplugin.json")

#include "runtimeplugin.moc"
