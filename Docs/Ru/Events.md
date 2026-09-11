# Events

## Содержание

1. [Lifecycle](#lifecycle)
2. [Async node](#async-node)
3. [Event Interval и FieldNotify](#event-interval-и-fieldnotify)
4. [Обновление текста](#обновление-текста)
5. [Рекомендации](#рекомендации)
6. [Производительность и тестирование](#производительность-и-тестирование)

## Lifecycle

Transition может сообщать фактическое `Transition Value` в трёх точках:

- `Started` — transition начал работу;
- `Updated` — новое sampled value доступно по Event Interval;
- `Finished` — target достигнут и transition завершён.

Lifecycle не является обязательным для property binding: binding напрямую записывает sampled value в Widget Property.

## Async node

`Add Widget Transition Async` принимает тот же `FWidgetTransition`, что и обычный Add, но даёт execution outputs `Started`, `Updated` и `Finished`. Используйте его, если нужна логика после старта/окончания или значение требуется другому виджету.

Async action корректно завершает себя при отказе старта, отмене или invalid widget; `Finished` не используется как сигнал отмены.

## Event Interval и FieldNotify

`Event Interval` задаёт секунды между `Updated` callbacks и FieldNotify broadcasts. `0` сохраняет dispatch каждый tick; default `0.033` примерно соответствует 30 Hz.

При binding к FieldNotify property runtime может записать property и отправить notify с тем же интервалом. Это позволяет UI обновляться реактивно без отдельного polling.

## Обновление текста

| Подход | Когда выбирать |
| --- | --- |
| Прямая запись в `Updated` | Небольшой локальный счётчик, где значение не нужно другим системам. |
| External text binding | Когда уже есть собственный getter, но polling допустим. |
| FieldNotify | Когда несколько UMG-потребителей должны получать изменение property. |
| ViewModel | Когда число — часть состояния экрана, а не детали конкретного widget. |

Для счётчика используйте Float transition и `As Float` в обработчике. Преобразование в `FText` оставляйте в presentation layer. Уменьшайте Event Interval, если пользователю не нужна перекадровая точность.

## Рекомендации

- Не назначайте `Updated` каждому transition «на всякий случай»: dynamic callback заметно дороже sampling.
- Сначала выберите владельца данных. Widget-local value — callback; shared state — FieldNotify или ViewModel.
- `Started` и `Finished` дешевле continuous `Updated`; для большинства UX-задач достаточно их.
- Обработчик callback может менять transition system, поэтому runtime dispatch защищён от reentrancy. Не опирайтесь на ссылку на внутренний transition после callback.

## Производительность и тестирование

`Performance.Callbacks`, `EventInterval`, `ExternalTextBinding`, `FieldNotifyTextBinding` и `AsyncTextCounter` сравнивают реальные способы доставки значения. Историческая карта оптимизации callbacks: [Async callback optimization](History/AsyncCallbackOptimization.md).

Корректность защищают `CallbackCancellation`, `RepeatCallbackDelay`, `CallbackReentrancy`, `EventInterval` и `NativeBuilder`. Методика и результаты: [Тестирование](Testing.md).
