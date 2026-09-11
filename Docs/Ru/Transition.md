# Transition

## Содержание

1. [Модель](#модель)
2. [Свойства и binding](#свойства-и-binding)
3. [From](#from)
4. [Повтор и направление](#повтор-и-направление)
5. [Режимы добавления](#режимы-добавления)
6. [ColorMix](#colormix)
7. [Производительность и тестирование](#производительность-и-тестирование)

## Модель

`FWidgetTransition` — Blueprint-facing описание и runtime instance. Pure nodes возвращают новую структуру, поэтому граф можно безопасно компоновать без скрытой мутации исходного Transition.

`Transition Value` сохраняет до четырёх каналов и semantic type: Float, Vector или Color. Используйте `Make … Transition Value` для входных значений и `As …` для значения из Events.

## Свойства и binding

`Create Widget Transition` принимает обязательные Widget, Widget Property, To Value, Time и Delay. Виджет обязан быть валиден в момент запуска. Binding кешируется при добавлении в систему: transition не ищет property path каждый tick.

Поддерживаются compatible float, Vector2D и LinearColor properties, выбранные slot properties, material scalar/vector parameters и быстрые adapters для:

- `RenderOpacity`;
- `RenderTransform.Translation`, `Scale`, `Shear`, `Angle`;
- `RenderTransformPivot`.

Если свойство использует FieldNotify, runtime может отправлять notification по `Event Interval`. Подробности — в [Events](Events.md).

## From

`From(Transition, Use From, From Value, Ignore Delay)` задаёт явную стартовую точку.

- При `Ignore Delay = true` значение записывается сразу, до initial delay.
- При `Ignore Delay = false` значение применяется после delay.
- Если From не задан, runtime читает текущее значение binding при старте.

Это удобно для enter-анимации, когда виджет должен сначала оказаться вне кадра или иметь нулевую opacity.

## Повтор и направление

- `Repeat Count = 0` — один проход.
- Положительное значение — число дополнительных проходов.
- `-1` — бесконечный repeat.
- `Yo Yo` меняет направление каждого следующего цикла.
- `Repeat Delay` повторно применяет Delay перед каждым повторным циклом.

Не используйте бесконечный repeat для фонового декоративного движения без необходимости: он остаётся активной runtime-работой.

## Режимы добавления

Режим работает в пределах пары `(Widget, Widget Property)`:

| Режим | Поведение |
| --- | --- |
| `Replace` | заменяет активный transition этого свойства; spring переносит текущее state для непрерывности. |
| `Skip` | не добавляет transition, если свойство уже активно или имеет Pipe-очередь. |
| `Pipe` | ставит transition в FIFO-очередь и запускает после завершения предыдущего. |

`Pipe` создаёт spring только когда queued transition реально запускается. Новый `Replace` очищает очередь Pipe для того же свойства.

## ColorMix

`ColorMix` выбирает пространство, в котором интерполируются два цвета: `RGB`, `HSV` или `OKLCH`. Это влияет на траекторию промежуточных оттенков, а не на target color.

Подробнее: [Смешение цветов](ColorMix.md).

## Производительность и тестирование

Основная цена linear transition низкая; выбор binding и наличие callbacks заметнее. Для frequently used UMG fields используются быстрые adapters, а для Pipe применена keyed FIFO queue.

- Корректность: `Runtime.SpecBuilders`, `PropertyBinding.Channels`, `ExplicitFrom`, `AddMode`, `PipeOrder`, `YoYo`.
- Измерения binding/Pipe: [исторические performance tests](History/PerformanceTests.md) и [история оптимизаций](History/OptimizationHistory.md).
- Методика и актуальный baseline: [Тестирование](Testing.md).
