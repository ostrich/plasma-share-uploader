#include "targetdefinition.h"

QString TargetDefinition::id() const
{
    return target.core.id;
}

QString TargetDefinition::displayName() const
{
    return target.core.displayName;
}

QString TargetDefinition::description() const
{
    return target.core.description;
}

QString TargetDefinition::icon() const
{
    return target.core.icon;
}

QStringList TargetDefinition::constraints() const
{
    return target.core.constraints;
}

QStringList TargetDefinition::extensions() const
{
    return target.core.extensions;
}
