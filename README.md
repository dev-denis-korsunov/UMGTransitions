# ElasticUMG: README and usage guide

ElasticUMG — плагин для Unreal Engine, который позволяет анимировать свойства виджетов UMG через плавные переходы (float-поля) с поддержкой типа"эльастик" и настраиваемых кривых. Весь функционал доступен как Blueprint-функции через UWidgetTransitionFunctionLibrary и через Editor-утилиты плагина.

Чтобы начать работать с плагином, убедитесь, что он включен в ваш проект и собирается вместе с вашей сборкой Unreal.

## Что внутри плагина
- Модуль ElasticUMG: набор функций для управления переходами свойств виджетов.
- Модуль ElasticUMGEditor: редакторские инструменты (пользовательские пины для выбора свойств), чтобы упрощать работу в редакторе.
- Реализация Transition-логики в `WidgetTransition`, включая обычные свойства (RenderTransform, Opacity и т.д.) и свойства слотов (Slot.Padding, Slot.Position, Slot.Size и т.д.).
- Поддержка пользовательских свойств через `FCustomFloatTransitionProperty` и делегаты обновления.
- Поддержка «Spring»-переходов через `FSpringFloat` для более естественной анимации.

## Как использовать (Blueprint)
1. Включите плагин ElasticUMG в проекте (Plugins -> ElasticUMG -> Enable).
2. В любом Blueprint виджета (или в Graph-Blueprint, где есть доступ к виджету) используйте функции из `WidgetTransitionFunctionLibrary`:
   - CreateWidgetTransition(WidgetProperty, TargetValue, Time, Delay, Interpolation)
   - CreateWidgetPropertyTransition(InWidget, InPropertyPath, TargetValue, Time, Delay, Interpolation)
   - CreateWidgetCustomFloatTransition(CustomProperty, TargetValue, Time, Delay, Interpolation)
   - AddWidgetTransition(WorldContextObject, UserWidget, Transition)
   - AddWidgetTransitionArray(WorldContextObject, UserWidget, Transitions)
   - ClearAllWidgetTransitions(WorldContextObject, UserWidget)
   - Curve(Transition, Curve) и др.

3. Пример: плавно изменить X-координату Translнации виджета на 100 за 0.5 сек.
   - Transition = CreateWidgetPropertyTransition(MyWidget, "RenderTransform.Translation.X", 100.0, 0.5, 0.0, nullptr)
   - AddWidgetTransition(GetWorld(), MyWidget, Transition)

4. Чтобы собрать несколько переходов последовательно или параллельно, используйте From / Pipe / RemoveFromParent / Curve и т.д. Концепции аналогичны API-подходу.

## API обзор (ключевые функции)
- CreateWidgetTransition(WidgetProperty, TargetValue, Time, Delay, Interpolation)
- CreateWidgetPropertyTransition(InWidget, InPropertyPath, TargetValue, Time, Delay, Interpolation)
- CreateWidgetCustomFloatTransition(const FCustomFloatTransitionProperty& CustomProperty, ...)
- AddWidgetTransition(WorldContextObject, UserWidget, Transition)
- AddWidgetTransitionArray(WorldContextObject, UserWidget, TransitionArray)
- ClearAllWidgetTransitions(WorldContextObject, UserWidget)
- From / Pipe / RemoveFromParent / Curve / Visibility / ToVisibility / FromVisibility
- Spring(...) — включение упругой синхронизации через FSpringFloat
- SetCustomFloatTransitionPropertyDelegate / SetCustomFloatTransitionPropertyValue / SetCustomFloatTransitionPropertyCompleteDelegate / GetCustomFloatTransitionPropertyValue
- EqualEqual_TrsCustomPropTrsCustomProp — сравнение двух переходов
- GetWidgetAnimatableFloatProperties / GetAvailableWidgetProperties
- GetWidgetSlotInfo — получить список слотов и их float-свойств

Часть функций доступна как Blueprint (UFUNCTION(BlueprintCallable|BlueprintPure)). Это позволяет вливать анимацию напрямую в ваши виджеты без шимирования кода.

## Взаимодействие с Editor
- ElasticUMGEditor регистрирует специальный Pin Factory для свойств виджетов, чтобы удобно подбирать путь к свойству в редакторе графов.
- В проекте должен быть доступен редакторский модуль; при включении плагина редакторские инструменты активируются автоматически.

## Примеры использования (C++ точка входа)
```cpp
// Внутри вашего виджета/актора
UWidget* MyWidget = /* получаем виджет */;
FWidgetTransition Transition = UWidgetTransitionFunctionLibrary::CreateWidgetPropertyTransition(
    MyWidget,
    TEXT("RenderTransform.Translation.X"),
    200.0f, // TargetValue
    0.8f,    // Time
    0.0f,    // Delay
    nullptr  // InterpolationCurve
);
UWidgetTransitionFunctionLibrary::AddWidgetTransition(this, MyWidget, Transition);
```

## Важные нюансы
- Переходы работают только для float-значений свойств (рефлекшен-обращение к FProperty/FStructProperty).
- Возможны переходы в слоты и их свойствам (Padding, Alignment, Position, Size, Anchors и пр.).
- Можно комбинировать переходы, используя Pipe/From/Curve и др. для построения цепочек анимаций.
- Встроенный Spring-механизм позволяет получить более «натуральные» движения за счет физического моделирования.
- В случае ошибок проверьте, что имя свойства/path корректно разборывается и свойство действительно является float-значением или содержит вложенные float-поля.

## Известные ограничения
- Набор свойств может не содержать некоторых пользовательских кастомных свойств — доступна только рефлексия и обработка float-полей.
- Нюансы доступа к полям в Slot (Padding, Position и т.д.) зависят от конкретного типа панели, используемой в виджете.

## Как собрать
- Убедитесь, что проект компилируется под Unreal Engine 5.6 или совместимую версию.
- Включите плагин ElasticUMG в вашем проекте (Plugins -> ElasticUMG -> Enable) и соберите проект через Unreal Editor или через UnrealBuildTool/CI.
- Рекомендуется запускать сборку в режиме Development для отладки и логирования.

## Быстрый старт (повторяемый)
- Включите ElasticUMG в проекте.
- Откройте Blueprint или C++ и попробуйте сделать первый переход:
  1) Создайте Transition через CreateWidgetPropertyTransition (или CreateWidgetTransition для базового).
  2) Добавьте переход через AddWidgetTransition.
  3) При необходимости комбинируйте Pipe/From/Curve для цепочек переходов.
- Проверьте вывод логов: категории LogWidgetTransition2 в UE_LOG.

## API обзор (детализация)
- CreateWidgetTransition(
  EWidgetProperty WidgetProperty = EWidgetProperty::TranslationX,
  float TargetValue = 0.0f,
  float Time = 0.0f,
  float Delay = 0.0f,
  UCurveFloat* Interpolation = nullptr
): FWidgetTransition
- CreateWidgetPropertyTransition(UWidget* InWidget, const FString& InPropertyPath, float TargetValue = 0.0f, float Time = 0.0f, float Delay = 0.0f, UCurveFloat* Interpolation = nullptr): FWidgetTransition
- CreateWidgetCustomFloatTransition(const FCustomFloatTransitionProperty& CustomProperty, float TargetValue = 0.0f, float Time = 0.0f, float Delay = 0.0f, UCurveFloat* Interpolation = nullptr): FWidgetTransition
- AddWidgetTransition(WorldContextObject, UserWidget, Transition)
- AddWidgetTransitionArray(WorldContextObject, UserWidget, TransitionArray)
- ClearAllWidgetTransitions(WorldContextObject, UserWidget)
- From / Pipe / RemoveFromParent / Curve / Visibility / ToVisibility / FromVisibility / Modifier / Spring
- GetWidgetAnimatableFloatProperties / GetAvailableWidgetProperties
- GetWidgetSlotInfo(Widget, SlotClassName, SlotFloatProperties)
- SetCustomFloatTransitionPropertyDelegate / SetCustomFloatTransitionPropertyValue / SetCustomFloatTransitionPropertyCompleteDelegate / GetCustomFloatTransitionPropertyValue
- EqualEqual_TrsCustomPropTrsCustomProp

## Примеры использования Blueprint
- Пример 1: плавное изменение RenderTransform.Translation.X внутри Widgets
  - Transition = CreateWidgetPropertyTransition(MyWidget, "RenderTransform.Translation.X", 200.0, 0.8, 0.0, nullptr)
  - AddWidgetTransition(GetWorld(), MyWidget, Transition)
  - Optional: Curve(Transition, MyCurve) для плавной кривой
- Пример 2: изменение Padding слота CanvasPanelSlot Left
  - Transition = CreateWidgetPropertyTransition(MyWidget, "Slot.Padding.Left", 10.0, 0.3, 0.0, nullptr)
  - AddWidgetTransition(GetWorld(), MyWidget, Transition)

## Примеры использования C++
- Пример 1: аналогично Blueprint, но через вызов CreateWidgetPropertyTransition и AddWidgetTransition из C++
- Пример 2: создание кастомного перехода с использованием FCustomFloatTransitionProperty

## Отладка и логи
- Включайте вывод логов через UE_LOG(LogWidgetTransition2, Log/Verbose) для визуализации цепочек и ошибок.
- В некоторых случаях полезно проверить путь свойства: RenderTransform.Translation.X или Slot.Padding.Left.
- Если путь содержит вложенные структуры, убедитесь, что они поддерживаются рефлексией UE4/5 и что свойства помечены CPF_Edit, если требуется редактор.

## Editor-подсистема
- ElasticUMGEditor регистрирует WidgetPropertyPinFactory для удобного выбора свойств в редакторе.
- При запуске редактора плагин добавляет соответствующие пины, чтобы облегчить сборку путей свойств напрямую в нодах.

## Совместимость и лицензия
- Плагин рассчитан на UE5.6+; некоторые детали могут зависеть от версии. Проверьте совместимость в вашем проекте.
- Лицензия: см. LICENSE в репозитории проекта (если доступно). Если нет, используйте только для некоммерческих целей или по согласованию с автором.

## История изменений
- Добавлен базовый README и дальнейшее расширение API-справки.

- Убедитесь, что проект компилируется под Unreal Engine 5.6 или совместимую версию.
- Включите плагин ElasticUMG в вашем проекте (Plugins -> ElasticUMG -> Enable) и соберите проект через Unreal Editor или через UnrealBuildTool/CI.

## Контакты и вклад
- Автор плагина: Denis Korsunov (см. файл .uplugin для сведений).
- Поддержка редакторских функций доступна через ElasticUMGEditor.

---
Это черновой черновик README. При необходимости можно дополнить примеры на Blueprint и добавить инструкции по деплою.
