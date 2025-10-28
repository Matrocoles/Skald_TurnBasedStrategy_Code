#pragma once

#include "CoreMinimal.h"
#include "UObject/UnrealType.h"

namespace Skald::PropertyAccess {

inline int32 ReadIntProperty(UObject *Object, const FName PropertyName,
                             int32 DefaultValue = 0) {
  if (!Object) {
    return DefaultValue;
  }

  if (const FIntProperty *Property =
          FindFProperty<FIntProperty>(Object->GetClass(), PropertyName)) {
    return Property->GetPropertyValue_InContainer(Object);
  }

  return DefaultValue;
}

inline bool ReadBoolProperty(UObject *Object, const FName PropertyName,
                             bool bDefaultValue = false) {
  if (!Object) {
    return bDefaultValue;
  }

  if (const FBoolProperty *Property =
          FindFProperty<FBoolProperty>(Object->GetClass(), PropertyName)) {
    return Property->GetPropertyValue_InContainer(Object);
  }

  return bDefaultValue;
}

inline void WriteIntProperty(UObject *Object, const FName PropertyName,
                             int32 Value) {
  if (!Object) {
    return;
  }

  if (FIntProperty *Property =
          FindFProperty<FIntProperty>(Object->GetClass(), PropertyName)) {
    Property->SetPropertyValue_InContainer(Object, Value);
  }
}

inline void WriteBoolProperty(UObject *Object, const FName PropertyName,
                              bool bValue) {
  if (!Object) {
    return;
  }

  if (FBoolProperty *Property =
          FindFProperty<FBoolProperty>(Object->GetClass(), PropertyName)) {
    Property->SetPropertyValue_InContainer(Object, bValue);
  }
}

} // namespace Skald::PropertyAccess
