#include "targetdefinition.h"

QString TargetDefinition::id() const
{
    return core.id;
}

QString TargetDefinition::displayName() const
{
    return core.displayName;
}

QString TargetDefinition::description() const
{
    return core.description;
}

QString TargetDefinition::icon() const
{
    return core.icon;
}

QStringList TargetDefinition::constraints() const
{
    return core.constraints;
}

QStringList TargetDefinition::extensions() const
{
    return core.extensions;
}
