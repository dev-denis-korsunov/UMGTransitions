# Events

## Содержание

- [Lifecycle](#lifecycle)
- [Async node](#async-node)
- [Event Interval и FieldNotify](#event-interval-и-fieldnotify)
- [Обновление текста](#обновление-текста)
- [Производительность и тестирование](#производительность-и-тестирование)
  - [Стоимость подписок](#стоимость-подписок)
  - [Event Interval](#event-interval)
  - [Этапы dispatch](#этапы-dispatch)
  - [Корректность](#корректность)
  - [Текстовые потребители](#текстовые-потребители)



## Lifecycle

Transition может сообщать фактическое `Transition Value` в трёх точках:

- `Started` — transition начал работу;
- `Updated` — новое sampled value доступно по Event Interval;
- `Finished` — target достигнут и transition завершён.

Lifecycle не является обязательным для property binding: binding напрямую записывает sampled value в Widget Property.

## Async node

`Add Widget Transition Async` принимает тот же `FWidgetTransition`, что и обычный Add, но даёт execution outputs `Started`, `Updated` и `Finished`. Используйте его, если нужна логика после старта/окончания или значение требуется другому виджету.

| Исход | Событие и освобождение |
| --- | --- |
| Нормальное завершение | Finished с конечным value, затем освобождение action |
| Отказ старта / нет context или subsystem | Finished с подготовленным EventValue, затем освобождение; target мог не быть достигнут |
| Clear / Replace / invalid widget после старта | Освобождение async owner без публичного Finished |

Finished не является универсальным подтверждением достижения target: отказ запуска использует тот же выход.

## Event Interval и FieldNotify

`Event Interval` задаёт секунды между `Updated` callbacks и FieldNotify broadcasts. `0` сохраняет dispatch каждый tick; default `0.033` ограничивает частоту примерно 30 Hz при достаточно частом tick; фактическая доставка привязана к кадрам.

При binding к FieldNotify property runtime может записать property и отправить notify с тем же интервалом. Это позволяет UI обновляться реактивно без отдельного polling.

## Обновление текста

| Подход | Когда выбирать |
| --- | --- |
| Прямая запись в `Updated` | Небольшой локальный счётчик, где значение не нужно другим системам. |
| External text binding | Когда уже есть собственный getter, но polling допустим. |
| FieldNotify | Когда несколько UMG-потребителей должны получать изменение property. |
| ViewModel | Когда число — часть состояния экрана, а не детали конкретного widget. |

Для счётчика используйте Float transition и `As Float` в обработчике. Преобразование в `FText` оставляйте в presentation layer. Увеличивайте Event Interval, если пользователю не нужна перекадровая точность.


## Производительность и тестирование

### Стоимость подписок

25.08.2026, UE 5.7.4 / Mac arm64 Development, 300 кадров. Единицы — мкс/кадр. Lifecycle-only измеряет установившийся tick между событиями: сами вызовы Started/Finished сюда не входят.

| Количество | Без событий | Lifecycle-only | Updated | Без событий + binding | Lifecycle + binding | Updated + binding |
| ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| 20 | 0.117 | 0.107 | 5.479 | 0.213 | 0.215 | 5.741 |
| 30 | 0.147 | 0.147 | 8.229 | 0.315 | 0.316 | 8.435 |
| 50 | 0.239 | 0.254 | 13.698 | 0.558 | 0.555 | 14.186 |
| 100 | 0.503 | 0.537 | 27.771 | 1.143 | 1.190 | 28.043 |

Стресс-замер той же серии, 500 переходов: без binding **2.648 → 134.044**, с binding **5.424 → 138.431 мкс/кадр** при подключении Updated. Добавленная стоимость — примерно 0.26 мкс на подписку за кадр.

**Вывод:** для реакции только на начало/окончание подключайте lifecycle. Continuous Updated нужен потребителям промежуточного значения; при массовых подписках его частота определяет бюджет. Подробнее: [методика callbacks](History/PerformanceTests.md#callback-baseline).

### Event Interval

26.08.2026, UE 5.7.4 / Mac arm64 Development, 100 linear transition без binding, 300 кадров при 60 FPS. Историческое имя параметра — Update Interval; сейчас Event Interval.

| Интервал, с | Мкс/кадр | Изменение относительно 0 |
| ---: | ---: | ---: |
| 0 | 28.333 | — |
| 0.033 | 15.140 | −46.6% |
| 0.050 | 10.233 | −63.9% |
| 0.100 | 5.298 | −81.3% |

**Вывод:** увеличение интервала сокращает dispatch, сохраняя расчёт transition каждый tick. 0.033 — исходный выбор для счётчика; 0.05–0.1 подходит, если редкие обновления визуально приемлемы. При hitch не воспроизводится очередь пропущенных callbacks, а финальное значение доставляется принудительно. Подробнее: [interval-замер](History/PerformanceTests.md#update-interval-baseline).

### Этапы dispatch

| Этап | Что изменилось | Результат и решение |
| --- | --- | --- |
| Hot/cold split | Lifecycle отделён от Updated | Убран перекадровый dispatch/lookup для lifecycle-only; таблица выше |
| Dense registry | Map заменён плотным storage | 100 Updated без/с binding: 27.771/28.043 → 26.137/26.680 мкс/кадр, серия 25–31.08; не повторный A/B |
| Sidecar | Callback links и states вынесены из transition | Transition 272 → 256 B, обязательный link 8 B |
| Pull/hybrid | Проверены повторный sample и сохранённый sample | См. отдельное сравнение Editor ниже |
| Value-only | Убраны widget/progress из payload | Async получает готовый value; API принят как компромисс памяти и удобства |

Открытый Editor, UE 5.7 / Mac arm64 Development, 31.08.2026; 100 переходов. Эти числа нельзя объединять с commandlet-таблицами выше.

| Вариант | Updated без binding, мкс/кадр | С binding | Async counter, мкс/кадр |
| --- | ---: | ---: | ---: |
| Pull | 83.676 | 85.329 | не записано |
| Hybrid | 79.678 | 80.243 | 83.403 |
| Value-only, запуск 1 | 83.256 | 84.086 | 80.535 |
| Value-only, запуск 2 | 83.812 | 86.959 | 79.750 |

**Вывод:** value-only не дал ускорения Updated относительно hybrid в этой серии, но сократил работу async counter. Нельзя описывать этот этап как универсальную оптимизацию скорости.

Ближайшие command-line прогоны до/после cleanup, 100 linear; одинаковый тип запуска, без строгого повторного A/B двух бинарников:

| Сценарий | До, мкс/кадр | После, мкс/кадр |
| --- | ---: | ---: |
| Без binding/callbacks | 0.513 | 0.476 |
| Lifecycle без binding | 2.883 | 0.489 |
| Updated без binding | 28.403 | 26.633 |
| Binding без callbacks | 1.098 | 1.091 |
| Binding + lifecycle | 3.544 | 1.138 |
| Binding + Updated | 29.309 | 28.200 |

**Вывод:** основная польза cleanup — исчезновение постоянной цены lifecycle; дальнейшее ускорение массового Updated следует искать прежде всего в частоте событий. Подробнее: [история dispatch и памяти](History/OptimizationHistory.md#callback-hotcold-split).

### Корректность

| Тест Runtime | Проверяемое поведение |
| --- | --- |
| CallbackCancellation | Clear, Replace и invalid widget освобождают async owner |
| RepeatCallbackDelay | Значение конца цикла доставляется до следующего delay |
| EventInterval | Throttling и обязательное конечное значение |
| CallbackReentrancy | Удаление своего transition и рост массива из callback |
| NativeBuilder | Доставка value в Started/Updated/Finished |

В журнале 09.09.2026 callback review: 13/13 runtime-тестов прошли; это результат той ревизии, не новый прогон документации. [Полный журнал](History/TestResults.md).

### Текстовые потребители

Сравнение direct SetText, async, polling и FieldNotify с условиями замеров находится непосредственно в [Использовании: стоимость счётчика](Usage.md#стоимость-счётчика). ViewModel не имеет подтверждённого полного MVVM baseline в сохранённом отчёте.
