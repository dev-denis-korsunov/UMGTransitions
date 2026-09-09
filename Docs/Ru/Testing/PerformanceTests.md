# Performance-тесты

Все тесты ниже имеют `PerfFilter` и не являются обычными unit-тестами. Они измеряются в Development Editor на одной машине; сопоставлять можно только одинаковые commandlet-прогоны, потому что абсолютные значения чувствительны к нагрузке процесса и ОС.

## Набор и методика

| Тест | Что делает | Зачем нужен |
| --- | --- | --- |
| `Performance.Construction` | 100 000 раз сравнивает ручную сборку структуры, `Create Widget Transition` и полную pure-цепочку. | Показывает цену удобного Blueprint API до попадания transition в subsystem. |
| `Performance.ConcurrentTick` | После прогрева выполняет 300 кадров по `1/60` для 1, 10, 100 и 500 transition с `UImage.RenderOpacity` binding. | Даёт общую картину стоимости hot path вместе с применением свойства. |
| `Performance.Callbacks` | Сравнивает 20, 30, 50 и 100 linear transition без callback, с lifecycle-only (`Started` + `Finished`) и с `Updated` dynamic delegate, с binding и без него; каждый случай измеряется 300 кадров после прогрева. | Проверяет hot/cold split callback-ов и изолирует цену registry, копирования delegate, dispatch и reentrancy-проверки. |
| `Performance.UpdateInterval` | 100 linear transition с bound `Updated`, без binding property, при интервале 0, `1/30`, `1/20` и `0.1` секунды. | Проверяет, что time-based throttling callback dispatch масштабирует hot-path стоимость ожидаемо и не зависит от FPS. |
| `Performance.ExternalTextBinding` | 100 `UUserWidget.CounterValue` float property обновляются transition через reflective binding; 100 `UTextBlock.TextDelegate` читают значение и форматируют `FText`. | Измеряет сценарий внешнего numeric property binding, который отображается текстом без async `Updated` callback. |
| `Performance.FieldNotifyTextBinding` | 100 FieldNotify `CounterValue` property обновляются transition, после записи subsystem broadcast-ит field change; native watcher обновляет `UTextBlock` с `UpdateInterval = 0.033` s. | Проверяет push-модель UMG/MVVM без polling text getter и с ограниченной частотой text update. |
| `Performance.AsyncTextCounter` | Измеряет 100 async transition без `Widget Property`: output `Updated` интерполирует float `0→100`, а receiver записывает целое значение в `UTextBlock`. | Показывает цену пользовательского обновления счётчика без property binding плагина. |
| `Performance.ModeMatrix` | 300 кадров для Linear, CurveTable easing и Spring, с binding и без него, при 100 и 500 transition. CurveTable содержит `(0,0)`, `(0.5,0.2)`, `(1,1)`. | Главный сравнительный тест алгоритмов. Вариант без binding выделяет математику; с binding показывает цену типичного использования. |
| `Performance.PipeHandoff` | В одном completion-кадре завершает active `Pipe` transition и запускает его queued successor: linear и spring, для 1, 20, 100 и 500 свойств. | Изолирует границу между двумя transition в очереди, не смешивая её с ценой создания и постановки в очередь. |
| `Performance.FastBindings` | 500 linear transition для каждого direct adapter: opacity, translation, scale, shear, angle, pivot. | Не даёт fast paths незаметно деградировать до reflective fallback. |

`FRealCurve` резолвится при добавлении transition в subsystem. Уже запущенный transition не подхватывает изменения CurveTable: его надо создать заново. Для сравнения spring-вариантов используйте строку **without binding** — запись свойства добавляет шум, не относящийся к симуляции.

## Граница Pipe

`Performance.PipeHandoff` замеряет именно кадр, в котором active transition завершился, был удалён, а ожидающий `Pipe` successor для того же widget property начал работу. Создание transition и постановка successor в очередь выполняются до запуска таймера. Для каждого случая тест делает 200 независимых выборок с 1, 20, 100 и 500 свойствами.

Есть два варианта successor:

- Linear: удаление, передача финального значения, резолв binding и активация следующего transition.
- Spring: тот же путь плюс ленивая инициализация `FWidgetTransitionSpring`. До завершения predecessor spring state у queued transition отсутствует.

В отчёте приведены цена всего completion-tick и цена одного handoff. В замер намеренно входят удаление из плотного массива и извлечение из очереди: это и есть наблюдаемая пользователем граница между двумя piped transition. Создание пар и сами вызовы `StartTransition` для постановки в очередь в замер не входят.

### Принятый baseline keyed FIFO

Прогон 2026-09-09, UE 5.7 / Mac arm64 Development, 200 samples. Очереди разделены по ключу `(Widget, Widget Property)`, у каждой свой head-index; перед запуском successor строится временный set активных ключей. Поэтому нет ни сдвига хвоста глобального массива, ни повторного линейного поиска active transition для каждой очереди.

| Successor | 1 | 20 | 100 | 500 |
| --- | ---: | ---: | ---: | ---: |
| Linear, μs/tick | 0.870 | 13.036 | 71.567 | 380.379 |
| Linear, μs/handoff | 0.870 | 0.652 | 0.716 | 0.761 |
| Spring, μs/tick | 0.954 | 14.007 | 70.268 | 381.599 |
| Spring, μs/handoff | 0.954 | 0.700 | 0.703 | 0.763 |

Burst из 500 handoff сократился с ~3.38 ms до ~0.38 ms (−88.8%). Spring state остаётся lazy: его старт не меняет порядок величин относительно linear successor.

## Callback baseline

Прогон 2026-08-25, UE 5.7.4 / Mac arm64 Development, 20–100 linear transition / 300 кадров:

| Count | No callback | Lifecycle-only | `Updated` | No callback + binding | Lifecycle-only + binding | `Updated` + binding |
| ---: | ---: | ---: | ---: | ---: | ---: |
| 20 | 0.117 | 0.107 | 5.479 | 0.213 | 0.215 | 5.741 |
| 30 | 0.147 | 0.147 | 8.229 | 0.315 | 0.316 | 8.435 |
| 50 | 0.239 | 0.254 | 13.698 | 0.558 | 0.555 | 14.186 |
| 100 | 0.503 | 0.537 | 27.771 | 1.143 | 1.190 | 28.043 |

Значения — μs/frame. Отдельные флаги `Started`, `Updated` и `Finished` убирают map lookup и dynamic delegate из hot path lifecycle-only transition: их результат в пределах шума совпадает с no-callback baseline. Цена bound `Updated` остаётся около `0.27–0.28 μs/transition/frame` и линейно масштабируется с числом подписок. Предыдущий стресс-прогон на 500 transition дал 134.044 μs/frame без binding и 138.431 с binding.

Stress-case, 500 linear transition / 300 кадров:

| Binding | Без callbacks, μs/frame | С bound `Updated`, μs/frame | Добавленная цена |
| --- | ---: | ---: | ---: |
| Нет | 2.648 | 134.044 | 131.396 μs/frame, ~0.263 μs/transition |
| `RenderOpacity` | 5.424 | 138.431 | 133.007 μs/frame, ~0.266 μs/transition |

Это стоимость реального Blueprint dynamic delegate: lookup в registry, копирование delegate, dispatch и вызов receiver. `Started` и `Finished` редки и не входят в hot-path benchmark. Не добавляйте `Updated` массово без необходимости; 500 подписок заметно дороже самой интерполяции.

## Исторический frame-rate baseline

Прогон 2026-08-25, UE 5.7.4 / Mac arm64 Development, 100 linear transition без property binding / 300 кадров. Удалённый `Update Every N Frames` ограничивал только bound `Updated`; вычисление transition и запись property не менялись.

| Frames | μs/frame | μs/transition |
| ---: | ---: | ---: |
| 1 | 27.515 | 0.275 |
| 2 | 14.094 | 0.141 |
| 3 | 9.291 | 0.093 |
| 6 | 5.102 | 0.051 |

Rate `2` почти вдвое сокращал hot-path стоимость callback. API заменён на `Update Interval` в секундах, поскольку frame rate зависит от FPS. Завершающий `Updated` выполняется принудительно, чтобы consumer всегда получил target, даже если transition закончился до следующего интервала.

## Update interval baseline

Прогон 2026-08-26, UE 5.7.4 / Mac arm64 Development, 100 linear transition без property binding / 300 кадров. `Update Interval` ограничивает bound `Updated` и FieldNotify broadcast; `0` сохраняет update каждый Tick, default async-ноды — `0.033` секунды.

| Interval, s | μs/frame | μs/transition |
| ---: | ---: | ---: |
| 0 | 28.333 | 0.283 |
| 0.033 (async default) | 15.140 | 0.151 |
| 0.050 (`1/20`) | 10.233 | 0.102 |
| 0.100 | 5.298 | 0.053 |

При 60 FPS default `0.033` выполняет примерно 30 callback/сек на transition и почти вдвое уменьшает цену `Updated` относительно per-frame режима. При перегруженном кадре система не догоняет пропущенные события: один callback получает актуальное значение. Завершающий `Updated` принудителен.

## External text binding baseline

Прогон 2026-08-26, UE 5.7.4 / Mac arm64 Development, 100 `UUserWidget.CounterValue` property transition с `FDynamicPropertyPath` и 100 `UTextBlock.TextDelegate` pull через `FText::AsNumber`:

| Сценарий | μs/frame | μs/widget |
| --- | ---: | ---: |
| External `CounterValue` binding + text pull | 30.956 | 0.310 |

Это модель обычного UMG binding: transition записывает число во внешний `UUserWidget` property, а text getter читает и форматирует его. Тест вызывает реальный dynamic `FGetText` delegate и потребляет результат; Slate layout, invalidation и paint в headless commandlet не исполняются, поэтому они не входят в число.

## FieldNotify text binding baseline (per tick)

Прогон 2026-08-26, UE 5.7.4 / Mac arm64 Development, 100 FieldNotify `UUserWidget.CounterValue` transition и 100 native watcher, обновляющих `UTextBlock`:

| Сценарий | μs/frame | μs/widget |
| --- | ---: | ---: |
| Polling `TextDelegate` | 30.956 | 0.310 |
| FieldNotify push | 20.956 | 0.210 |

Это исторический per-tick baseline. Теперь transition автоматически вызывает FieldNotify после успешной записи reflective property, но при заданном `UpdateInterval` уведомление ограничивается этим интервалом, а финальное значение уведомляется обязательно. Push-модель без interval примерно на 32% быстрее polling getter на этом сценарии: она использует native delegate и обновляет текст только как реакцию на изменение поля. `FFieldId` не хранится в каждом transition: binding сохраняет уже существующее имя поля и запрашивает id из class descriptor только для FieldNotify property, сохраняя layout `FWidgetTransitionPropertyBinding` 88 B и `FWidgetTransition` 272 B. Новый baseline с `UpdateInterval = 0.033` s будет добавлен после отдельного прогона.

## Как обновлять текст

`FWidgetTransition` интерполирует числовые и визуальные значения, а не `FText`. Поэтому текст всегда является consumer-ом числа: значение нужно отформатировать и передать в `UTextBlock::SetText`. Ниже — все практические пути для этого в проекте.

| Подход | Поток данных | Частота | Измеренный ориентир, 100 элементов | Когда выбирать |
| --- | --- | --- | ---: | --- |
| Прямой imperative код | Игровой код → `SetText` | Только когда код вызывает setter | 11.002 μs/frame | Статический текст, редкие изменения, простой одноразовый UI. Самый короткий и дешёвый путь. |
| Обычный UMG binding / `TextDelegate` | Transition → `UUserWidget.CounterValue` → getter `FText` → `TextBlock` | Pull при обновлении UI | 30.956 μs/frame | Быстрый Blueprint-прототип, когда частота обновления мала. Не подходит для множества счётчиков на каждом кадре. |
| Async `Updated` | Transition → Blueprint dynamic multicast → `SetText` | Каждый tick или `Update Interval` | 36.604 μs/frame | Когда текст — именно реакция на ход transition и требуется Blueprint-событие/дополнительная логика. Самый гибкий, но самый дорогой вариант. |
| FieldNotify на самом виджете | Transition → `UUserWidget.CounterValue` → FieldNotify → consumer → `SetText` | Push; каждый tick при interval `0`, либо заданный interval | 20.956 μs/frame, per-tick | Предпочтительный вариант для локального UI-состояния без отдельного источника данных. `UpdateInterval = 0.033–0.1` снижает число форматирований и invalidation. |
| MVVM ViewModel + FieldNotify | Система/игра → `UMVVMViewModelBase` setter → MVVM binding → `TextBlock` | Push; setter или throttled producer | Новый тест подготовлен, baseline ещё не записан | Предпочтительный вариант, когда данными владеет не конкретный widget: несколько экранов, переиспользование, тестируемая логика, разделение UI и gameplay. |
| Прямой transition binding в `Text` | — | — | — | Не применяется: поддерживаемые transition value — float, vector2D и color, а `FText` требует форматирования и локализации. Используйте один из путей выше. |

### Рекомендация

Для визуальных свойств (`RenderOpacity`, transform, material) bind transition напрямую: это самый дешёвый и плавный путь. Для текста, отображающего transition-значение, по умолчанию используйте `FieldNotify` и `UpdateInterval = 0.033` секунды. Если значение является данными игры, а не частным состоянием конкретного виджета, тем же способом обновляйте `UMVVMViewModelBase` и подключайте UMG MVVM binding.

`Updated` оставляйте для действительно событийной логики — например, звука, порогов, побочных эффектов или нескольких нестандартных consumer-ов. Обычный polling `TextDelegate` допустим для редких обновлений, но не должен быть стандартом для анимируемых счётчиков.

MVVM benchmark `ElasticUMGProject.UMGTransitions.Performance.MVVMFieldNotifyTextBinding` находится в модуле проекта, потому что `ModelViewViewModel` — опциональный engine plugin. Он измеряет setter `UMVVMViewModelBase` и FieldNotify delivery; для сравнения полного скомпилированного MVVM binding нужен отдельный тестовый `WidgetBlueprint` с реальным MVVM binding.

## Counter update baseline

Прогон 2026-08-25, UE 5.7.4 / Mac arm64 Development, 100 `UWidgetTransitionAsyncAction::HandleUpdated` / 300 кадров, `Widget Property = None`, float `0→100` и `UTextBlock::SetText(FText::AsNumber(...))` в receiver:

| Сценарий | μs/frame | μs/counter |
| --- | ---: | ---: |
| Plain counter: direct `SetText` | 11.002 | 0.110 |
| Async text counter без binding | 36.604 | 0.366 |
| Async path overhead | 25.602 | 0.256 |

Plain и async сценарии используют одинаковые 100 `UTextBlock`, последовательность `0→100`, `FText::AsNumber` и `SetText`. Async path дополнительно интерполирует `FromValue`/`ToValue` и dispatch-ит Blueprint dynamic multicast. Его разница с plain — 0.256 μs/counter — совпадает с core `Updated` callback baseline (~0.27 μs), значит binding здесь действительно ни при чём.

## Отклонённые эксперименты

`SystemUpdateById` проверял один subsystem-wide dynamic delegate, который передаёт id и progress, а receiver ищет `UTextBlock` в `TMap<TransitionId, UTextBlock>`. Для 100 transition он показал 35.844 μs/frame (0.358 μs/transition) против 36.604 μs/frame у async-счётчика. Выигрыш ~2% находится в пределах шума, поэтому test hook и benchmark удалены из кода. Центральный dispatcher не устраняет Blueprint `ProcessEvent` и `SetText`; возвращаться к этой архитектуре стоит только при отдельной выгоде для API или владения данными.

## Текущие ориентиры

Последний сопоставимый прогон после перехода на `TArray<FWidgetTransition>`:

| Сценарий, 500 transition | Без binding, μs/frame | С binding, μs/frame |
| --- | ---: | ---: |
| Linear | 2.340 | 5.499 |
| CurveTable easing | 2.351 | 5.493 |
| Spring | 6.402 | 9.297 |

Storage-измерения: `TArray` spring — 4.212 μs/frame против 4.670 у `TSparseArray`; transition `TArray` — 2.591 μs/frame, после 500 `RemoveAtSwap` — 2.563. Это исторические эксперименты; их исходники и статус — в [Experiments.md](Experiments.md). Подробнее о причинах решений — в [OptimizationHistory.md](OptimizationHistory.md); полный журнал — в [TestResults.md](../TestResults.md).

Изолированные эксперименты индексов: специализированные mode passes в трёх независимых прогонах — в среднем 1.268 против 1.537 μs/frame для mixed pass (−17.5%); prebuilt dirty widget groups — 1.656 против 1.707 μs/frame (−3.0%). Mode benchmark всё ещё не включает реальный tick spring, binding, callbacks и удаление. Их границы и решение описаны в [ModeAndDirtyIndexExperiments.md](ModeAndDirtyIndexExperiments.md).

## Правило обновления

1. Собрать Development Editor.
2. Запустить ровно один perf-тест в новом commandlet-процессе.
3. Сохранить в журнале все строки `AddInfo`, конфигурацию и дату.
4. Не считать разницу в единицы процентов регрессией без повторного прогона.

Новый benchmark добавляется только вместе с описанием измеряемого пути и ответом на вопрос, какое архитектурное решение он должен подтвердить или опровергнуть.
