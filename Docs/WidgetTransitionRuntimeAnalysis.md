# FWidgetTransition: память и производительность

> Историческая часть ниже описывает раннюю typed-реализацию. Актуальные результаты и журнал измерений находятся в `TestResults.md`.

## Актуальное дополнение: fast widget property adapters (2026-08-23)

`FWidgetTransitionPropertyBinding` сначала распознаёт небольшой набор наиболее частых стандартных свойств `UWidget`, а затем использует прямые публичные getter/setter движка. Поддерживаются `RenderOpacity`, `RenderTransform.Translation`, `Scale`, `Shear`, `Angle` и `RenderTransformPivot`.

### Маршрут binding

```text
WidgetProperty
  ├─ стандартное имя UWidget → EWidgetTransitionBindingKind → UWidget::Get/Set...
  ├─ Material.<Parameter>    → cached UMaterialInstanceDynamic
  └─ любое другое имя        → FDynamicPropertyPath fallback
```

Fast route определяется один раз в `Resolve` при добавлении transition. В tick `Read`/`Apply` переходят по tag и не вызывают `PropertyPathHelpers`. Для fallback сохранены текущие возможности вложенных свойств, типовая проверка и material parameters.

### Почему используются методы, а не прямой доступ к UPROPERTY

Поля `UWidget::RenderOpacity` и `RenderTransform` помечены UE как deprecated для прямого доступа. Adapter вызывает `SetRenderOpacity`, `SetRenderTranslation`, `SetRenderScale`, `SetRenderShear`, `SetRenderTransformAngle` и `SetRenderTransformPivot`. Это сохраняет Slate invalidation и не зависит от внутреннего layout виджета.

### Измеренный эффект

В Development Editor, 500 linear transition, 300 кадров по `1/60`:

| Путь | Время на кадр | Время на transition |
| --- | ---: | ---: |
| `FDynamicPropertyPath` для `RenderOpacity` | 19.955 μs | ~0.040 μs |
| Fast `RenderOpacity` adapter | 5.880 μs | 0.012 μs |
| Fast transform/pivot adapters | ~5.7–6.1 μs | ~0.012 μs |

Абсолютные цифры commandlet могут меняться от нагрузки системы, но прямой adapter убирает reflection из hot-path. Inline-размер binding пока не меняется; fast routes дополнительно не аллоцируют сегменты `FDynamicPropertyPath`.

Дата анализа: 2026-08-02. Документ описывает текущую typed-реализацию в ветке `experiment-2-alpha-transitions` и является планом изменений, а не спецификацией уже внесённого рефакторинга.

## Вывод

Текущий `FWidgetTransition` действительно избыточен для ожидаемого числа одновременно работающих анимаций. При нескольких десятках переходов это не создаст заметной нагрузки по памяти, однако структура хранит много взаимно исключающих данных и из-за этого хуже помещается в кэш при обходе `TSparseArray`.

Наибольший практический выигрыш даст не ECS, а три локальных изменения:

1. Хранить один update-делегат, соответствующий типу значения, вместо четырёх.
2. Заменить две `TSharedPtr` пружин на один единолично владеемый optional runtime-state.
3. Вынести редкие callback-данные в sidecar-структуру, если реальный замер покажет, что callbacks обычно не подключены.

Битовые флаги стоит вернуть: это хороший способ закрепить набор состояний и оставить место для расширения. Самостоятельно они почти не изменят итоговый `sizeof`, так как структура выровнена по 8 байт.

## Текущее хранение

`FWidgetTransition` сейчас содержит:

| Группа | Данные | Оценка inline-памяти на 64-bit Mac |
| --- | --- | ---: |
| Идентификация | `TWeakObjectPtr<UWidget>`, `FString WidgetProperty` | ~24 B |
| Доступ к свойству | `FWidgetTransitionPropertyBinding` | ~72 B |
| Значения | `FromValue`, `ToValue` (`TVariant`) | ~48 B |
| Интерполяция | два `FVector2D` в `FWidgetTransitionEasingValue` | 32 B |
| Тайминг/повторы/spring controls | `Time`, `Delay`, `CurrentTime`, счётчики, speed/bounce | ~28 B |
| Spring handles | два `TSharedPtr`, хотя валиден максимум один | 32 B |
| Callbacks | `TransitionId` + pointer на optional Update sidecar; Start/Finished хранятся внешне | 16 B |
| Флаги и выравнивание | 5 `bool` и padding | несколько байт |

Первый automation baseline на UE 5.7.4 / macOS arm64 зафиксировал 440 B на экземпляр с выравниванием 8 B. После выноса `St / En` в `UWidgetTransitionSubsystem::EventCallbacks`, `Up` в optional sidecar, перехода на `FName`, compact flags и единого spring state текущий baseline — **224 B**. Основная причина первоначального расхождения с оценкой — `FOn*WidgetTransitionUpdate` и `FOnWidgetTransitionEvent` занимают по 32 B, а не 16 B. Размер не включает накладные расходы `TSparseArray`, сегментов `FDynamicPropertyPath` и heap-данных для bound callback/spring.

Для 100 активных переходов это порядка 22 KiB inline-данных; память сама по себе не проблема. Важнее, что hot-данные теперь существенно лучше помещаются в типичный L1 data cache, а обход выполняется каждый тик.

## Что уже есть в старой модели

В текущем `master` публичная старая модель ещё использует отдельные `bool`. Нужный набор битовых флагов находится в сохранённой предыдущей версии `WidgetTransition.h.bak`:

```cpp
uint8 bFrom : 1;
uint8 bChangeFromPropertyAfterDelay : 1;
uint8 bPipe : 1;
uint8 bFromVisibility : 1;
uint8 bToVisibility : 1;
uint8 bRemoveFromParent : 1;
uint8 bSpring : 1;
```

Не следует возвращать флаг только ради бинарной экономии, если у него пока нет поведения в текущем typed API. В частности, `Pipe`, `FromVisibility` и `ToVisibility` сейчас не имеют реализации. Полезно вернуть их как зарезервированные только в момент, когда возвращается функциональность; иначе они вводят ложное обещание API.

Рекомендуемый runtime-набор:

```cpp
uint16 bChangeFromPropertyAfterDelay : 1; // инверсия текущего bApplyValueBeforeDelay
uint16 bRemoveFromParent : 1;
uint16 bSpring : 1;
uint16 bYoYo : 1;
uint16 bStarted : 1;
uint16 bFrom : 1;            // только если переход действительно хранит режим From
uint16 bPipe : 1;            // вместе с реализацией pipe
uint16 bFromVisibility : 1;  // вместе с visibility API
uint16 bToVisibility : 1;    // вместе с visibility API
```

`bUseSpring` лучше переименовать в `bSpring`, а `bApplyValueBeforeDelay` — хранить как `bChangeFromPropertyAfterDelay`: старое имя соответствует ранее использованной модели и выражает исключение, а не дефолтное поведение. В Blueprint-параметре можно оставить позитивное имя `Apply Value Before Delay`; преобразование делается на границе API.

## Spring state

Сейчас одновременно выделены места под:

```cpp
TSharedPtr<FSpringFloat> FloatSpring;
TSharedPtr<FSpringVector2D> VectorSpring;
```

В каждый конкретный момент применяется только один из них. `TSharedPtr` здесь не нужен: state принадлежит ровно одному переходу, не передаётся между переходами и не требует разделённого владения.

Рекомендуемая форма:

```cpp
struct FWidgetTransitionSpringState
{
    TVariant<FSpringFloat, FSpringVector2D> Spring;
};

TUniquePtr<FWidgetTransitionSpringState> SpringState;
```

Это уменьшает inline-хранение с 32 B до одного указателя (8 B), убирает reference-count/control block и оставляет одно выделение только для spring-переходов. Альтернатива с двумя `TOptional` держит оба тяжёлых состояния inline и увеличит каждый обычный переход, поэтому её исключаем.

## Делегаты

Четыре typed update-делегата нужны API-слою, но в runtime у перехода используется ровно один. Сейчас остальные три занимают место впустую.

### Вариант A — один variant update-делегат

```cpp
using FWidgetTransitionUpdateCallback = TVariant<
    FOnFloatWidgetTransitionUpdate,
    FOnBoolWidgetTransitionUpdate,
    FOnVectorWidgetTransitionUpdate,
    FOnColorWidgetTransitionUpdate>;
```

В tick выполняется ветвление по `ValueType`, затем вызывается корректная альтернатива. Это сохраняет Blueprint-сигнатуры и убирает три неиспользуемых делегата. Этот вариант не выбран: он держит callback inline даже на переходах без `Up`.

### Вариант B — callback sidecar

```cpp
struct FWidgetTransitionCallbacks
{
    FWidgetTransitionUpdateCallback OnUpdate;
    FOnWidgetTransitionEvent OnStarted;
    FOnWidgetTransitionEvent OnFinished;
};

TUniquePtr<FWidgetTransitionCallbacks> Callbacks;
```

Lifecycle callbacks уже вынесены в registry subsystem. Пустой переход при sidecar для update хранит лишь 8 B указателя вместо 128 B четырёх update-полей, а sidecar создаётся только при bound `Up`. Выбран именно этот вариант. Его runtime payload — 40 B `TVariant` плюс heap-аллокация лишь у переходов с callback.

Если события используются почти всегда, Variant без sidecar проще и предсказуемее. Перед выбором нужен замер частоты реально bound callback в проекте.

## Прочие уплотнения

| Изменение | Выигрыш | Риск/замечание |
| --- | --- | --- |
| `FString WidgetProperty` → `FName PropertyPath` | ~8 B inline и нет отдельного string buffer | Преобразовывать в строку только для `FDynamicPropertyPath`; полный dotted path допустим как `FName`. |
| Не хранить `Widget` в `FWidgetTransitionPropertyBinding` | ~8 B | `Read/Apply` принимают `UWidget*` от перехода. Верхний `Widget` всё равно нужен для cleanup и сравнения. |
| Упаковать флаги в `uint16` | Главным образом место для будущих flags | Вероятно не уменьшит весь `sizeof` без перестановки полей. |
| Переместить флаги после scalars и перед pointer-полями | Убирает внутренний padding | Делать вместе с реальным `sizeof`-замером. |
| Union для easing и SpringSpeed/Bounce | До ~8 B | Эти данные взаимоисключающие. Реализовывать только если код остаётся читаемым; не использовать `TVariant`, так как его tag съест часть выгоды. |

`FDynamicPropertyPath` не стоит заменять простым `FProperty*`: он обеспечивает корректную работу с вложенными путями и кешированием UE. Он дорог (сама структура хранит массив сегментов и cached pointers), но рефлексия/property-path lookup гораздо важнее его inline-размера. Любая замена возможна только с явной проверкой вложенных свойств, invalidation и hot-reload.

## ECS / SoA

Полная ECS-модель или несколько параллельных массивов сейчас не оправданы:

- ожидается менее 100 одновременных переходов;
- основная работа тика — `PropertyPathHelpers::SetPropertyValue`, вычисление cubic Bezier/spring и потенциальный Blueprint delegate, а не итерация по структурам;
- ECS усложнит удаление, событийность, владение и диагностику, но почти не уменьшит стоимость этих операций.

Если после профилирования понадобится масштабирование, следующий шаг — не ECS, а hot/cold split. Он подробно описан ниже:

```text
TSparseArray<FWidgetTransitionRuntime>  // widget, property binding, values, timers, flags
TUniquePtr<FWidgetTransitionColdData>   // easing config, callbacks, spring state
```

Однако сначала нужно подтвердить, что анимаций действительно сотни и они упираются в CPU/cache. Для текущей цели локальное уплотнение сохраняет простую модель и даёт больший ROI.

## Hot/cold split

Hot/cold split не меняет публичный Blueprint API и не требует полноценной ECS. Это разделение одной runtime-записи на компактную часть, которую читает каждый tick, и необязательные данные, нужные только отдельным режимам.

### Цель

При обходе `TSparseArray` процессор загружает cache line целиком. Сейчас cache line с таймером и `From/To` соседствует с шестью делегатами, двумя shared pointers и easing-параметрами. Для обычной interpolation без callbacks и без spring большая часть загруженных байтов не участвует в вычислении.

После разделения основной цикл читает меньшую `FWidgetTransitionRuntime`. Cold-данные читаются только если активирован соответствующий режим. Это улучшает locality без смены алгоритма и не требует синхронизации нескольких массивов.

### Предлагаемые структуры

```cpp
struct FWidgetTransitionRuntime
{
    TWeakObjectPtr<UWidget> Widget;
    FName PropertyPath;
    FWidgetTransitionPropertyBinding PropertyBinding;
    FTransitionValue FromValue;
    FTransitionValue ToValue;
    float CurrentTime;
    float Delay;
    int32 RepeatCount;
    int32 CompletedRepeats;
    uint16 bChangeFromPropertyAfterDelay : 1;
    uint16 bRemoveFromParent : 1;
    uint16 bSpring : 1;
    uint16 bYoYo : 1;
    uint16 bStarted : 1;
    TUniquePtr<FWidgetTransitionColdData> ColdData;
};

struct FWidgetTransitionColdData
{
    FWidgetTransitionEasingValue Easing;
    FWidgetTransitionModeData ModeData; // Time либо spring controls
    TUniquePtr<FWidgetTransitionSpringState> SpringState;
    TOptional<FWidgetTransitionCallbacks> Callbacks;
};
```

Это схема, а не буквальный код: `ModeData` может быть union, а callbacks могут быть отдельным pointer, если они встречаются редко. Важное правило — hot-часть не должна содержать поля, без которых она может завершить обычный tick.

### Что остаётся hot

В горячей части должны оставаться только данные, нужные каждому активному переходу:

- валидность и адрес виджета;
- resolved property binding;
- текущие `From/To` и тип значения;
- `CurrentTime`, `Delay`, repeat-счётчики;
- флаги жизненного цикла и режима;
- идентификатор property path для замены уже существующего перехода.

`From/To` нельзя выносить в cold: они нужны каждому вычислению lerp и каждому restart/YoYo. Binding тоже лучше оставить hot: именно `Apply` является частью обычного тика.

### Что выносится cold

| Данные | Почему cold |
| --- | --- |
| `FWidgetTransitionEasingValue` | Не используется spring-режимом; в обычном режиме читается раз за tick, но может быть объединён с mode data. |
| Spring state | Нужен только float/vector spring; создаётся только для этого режима. |
| Update/Start/Finished callbacks | Часто не привязаны и не нужны для применения свойства. |
| Параметры Spring Speed/Bounce | Нужны только при создании/restart spring state. |
| Будущие pipe/visibility-параметры | Редкие специализированные режимы не должны увеличивать запись каждого перехода. |

`Easing` — пограничный случай. Если interpolation составляет почти все переходы, держать его hot проще и зачастую быстрее. Если будет единый `ModeData`-union, он остаётся inline, но не дублирует spring controls. Cold-перенос easing следует принимать только после профилирования.

### Жизненный цикл

1. **Создание.** Runtime-запись заполняется без heap-аллокации настолько, насколько возможно. `ColdData` создаётся, только когда нужен easing, spring или callback.
2. **Tick.** Обычный путь работает по hot-данным. Перед обращением к callback/spring выполняется короткая проверка `ColdData != nullptr` и флага режима.
3. **Restart / YoYo.** Меняются hot `From/To`, таймер и repeat. Для spring обращение к cold state создаёт или перезапускает один spring object.
4. **Завершение.** `TSparseArray` удаляет runtime-запись; `TUniquePtr` освобождает cold state вместе с callback/spring данными.
5. **Замена по тому же widget/property.** Сравнение использует hot `Widget` и `PropertyPath`, поэтому cold state предыдущего перехода уничтожается вместе с записью.

### Производительность и аллокации

Плюсы:

- меньше cache pressure при десятках и сотнях простых переходов;
- меньше inline-памяти в `TSparseArray`;
- callback и spring allocations отсутствуют у переходов, где их нет;
- меньше копируемых байтов при перемещении записи внутри контейнера.

Минусы:

- как минимум один pointer и branch на cold path;
- возможна heap-аллокация при создании spring/callback transition;
- дополнительная косвенность усложняет отладку и может быть медленнее, если почти все переходы используют callbacks и spring;
- нельзя допускать аллокаций в `TickTransitions`: все cold-объекты создаются в `CreateWidgetTransition` или `RestartTransition` до начала кадрового цикла.

При ожидаемых менее 100 активных анимациях hot/cold split имеет смысл только после простых уплотнений. Он оправдан, когда профайлер показывает, что tick переходов занимает заметное время или когда callback/spring режимы реально редки.

### Тестирование hot/cold split

Перед применением нужны отдельные automation-тесты:

- обычная interpolation без `ColdData`;
- interpolation с easing и `Up/St/En`;
- spring float и vector с restart/YoYo;
- delay с `Apply Value Before Delay` в обоих состояниях;
- удаление и замена перехода на том же widget/property;
- счётчик аллокаций: zero allocations во время `TickTransitions`.

Также в журнале тестов фиксируются `sizeof(FWidgetTransitionRuntime)`, размер cold-структур и время сценариев 10/100/500. Важно сравнивать не только среднее время, но и число аллокаций и p95 кадра.

## План измерений и рефакторинга

1. **Зафиксировать baseline.** В development build один раз логировать `sizeof` и `alignof` для `FWidgetTransition`, binding, value, easing, delegate, spring и `TSparseArray` allocation. Отдельно замерить external allocations на создание 100 переходов.
2. **Профилировать 10 / 100 / 500 переходов.** Три сценария: interpolation без callbacks, spring без callbacks, callbacks на каждом тике. Использовать Unreal Insights и `STAT`-цикл вокруг `TickTransitions`.
3. **Сделать безопасный structural pass.** Битовые флаги, порядок полей, `FName` для property path, удалить дублированный widget из binding. Сверить поведение split pins, delay и YoYo.
4. **Сделать spring pass.** Одна `TUniquePtr<FWidgetTransitionSpringState>`; проверить restart/YoYo и отсутствие аллокаций на каждом tick.
5. **Сделать callback pass.** Начать с Variant update-делегата. Sidecar применять только если измерение подтверждает, что callbacks редко bound.
6. **Сравнить до/после.** Текущий baseline — 224 B; исходный — 440 B. Цель safe-пакета достигнута. Полный hot/cold split имеет смысл только после CPU-профилирования сценариев 100/500 переходов; ожидаемая дополнительная экономия не должна оправдываться ценой сложности без такого сигнала.

## Критерии принятия

- Нет аллокаций во время `TickTransitions`.
- При 100 transitions стоимость tick не ухудшается; при 500 — улучшается или остаётся в пределах шума.
- Blueprint pins и поведение `Delay`, `Apply Value Before Delay`, `YoYo`, `Repeat`, `Spring`, `Up/St/En` неизменны.
- `FWidgetTransition` остаётся copy/move-safe для `TSparseArray`; использование `TUniquePtr` проверяется на корректное перемещение.
