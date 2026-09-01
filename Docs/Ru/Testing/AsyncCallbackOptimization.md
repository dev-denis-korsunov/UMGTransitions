# Карта оптимизации async callback-ов

Документ описывает async action и связанное runtime-хранилище callback-ов. Цель — уменьшить память и стоимость async-ноды, сохранив стандартные Blueprint execution outputs `Started`, `Updated` и `Finished`.

## Текущее устройство

`Activate()` подготавливает callback-пакет и передаёт его в subsystem:

| Данные | Где живут | Назначение |
| --- | --- | --- |
| `Started`, `Finished` | `FWidgetTransitionLifecycleCallbacks` | Редкие lifecycle-события |
| `Updated`, interval, latest sample, FieldNotify state | `FWidgetTransitionUpdateState` | Плотный update-pass |
| Индексы lifecycle/update | `FWidgetTransitionCallbackLinks` | Параллельный transition sidecar |
| `FWidgetTransitionCallbacks` | временный пакет создания | Передача delegate-ов из async-ноды в runtime |

Async action хранит только pending transition, текущее `EventValue` и world context. Started и Finished получают `FWidgetTransitionValue` непосредственно от runtime, поэтому из action удалены `FWidgetTransitionPropertyBinding` размером `88 B` и копия target value размером `32 B`. Все три Blueprint-выхода теперь имеют единый value-only контракт: From Value для Started, текущий sample для Updated и финальный To Value для Finished.

## Этапы

| Этап | Статус | Изменение | Ожидаемый эффект | Риск |
| --- | --- | --- | --- | --- |
| 0. Baseline | Завершён | Зафиксировать async/plain и callback benchmarks | Сравнимая точка отсчёта | Низкий |
| 1. Убрать постоянный callback-пакет | Завершён | Удалить поле `Callbacks` из `UWidgetTransitionAsyncAction`; создавать его локально в `Activate()` | Экономия `96 B` на async UObject | Низкий |
| 2. Проверить размер UObject | Реализован, нужен новый прогон | `Diagnostics.StorageLayout` выводит размер async action и временного lifecycle event | Подтвердить реальную экономию с учётом alignment/UObject | Низкий |
| 3. Уменьшить async value state | Завершён | Передавать Started/Finished value из runtime и удалить binding/target copy из action | Удалено `120 B` native-полей на action | Средний |
| 4. Async-specific dispatcher | Отложен | Заменить три dynamic delegate в runtime на компактную ссылку на async action и mask событий | Снижение runtime callback storage для async | Высокий |
| 5. Контроль lifetime | Обязателен для этапа 4 | Гарантировать, что async action не уничтожается до `Finished`, включая очистку widget и reentrancy | Исключить dangling UObject callback | Высокий |

Runtime sidecar реализован отдельно от async action: transition хранит только данные анимации, а callback links, lifecycle delegates и update state принадлежат subsystem. Принятый гибрид и результаты push/pull экспериментов описаны в [истории оптимизаций](OptimizationHistory.md).

## Этап 1: локальный пакет

До изменения `UWidgetTransitionAsyncAction` содержал `FWidgetTransitionCallbacks` как поле размером `96 B`. Пакет требовался только во время `Activate()` и сразу перемещался в subsystem. Теперь он создаётся локально:

```cpp
void UWidgetTransitionAsyncAction::Activate()
{
    FWidgetTransitionCallbacks Callbacks;
    // BindDynamic(...)
    StartTransition(Context, MoveTemp(PendingTransition), MoveTemp(Callbacks));
}
```

Это не меняет delegate bindings, индексы или порядок событий. Изменение принято и проверяется общей callback regression-группой.

## Этап 4: варианты компактного dispatcher-а

Рассматриваются три варианта:

1. **Shared async record.** Runtime хранит `TWeakObjectPtr<UWidgetTransitionAsyncAction>` и битовую маску нужных событий. Плюс — минимальный storage. Минус — runtime получает специальную ветку для async.
2. **Non-owning callback adapter.** Async action регистрирует небольшой C++ adapter с функциями `HandleStarted/Updated/Finished`. Плюс — нет трёх `FScriptDelegate` в runtime. Минус — сложнее lifetime и GC-сценарии.
3. **Оставить dynamic delegates.** Самый простой и UE-идиоматичный вариант; оптимизировать только UObject-поле и частоту `Updated` через `UpdateInterval`.

Предварительное решение: сначала принять варианты 1–2 только если замер покажет существенную цену runtime storage или dispatch. Dynamic Blueprint delegate — ожидаемый основной расход async update, поэтому уменьшение памяти не обязательно даст ускорение кадра.

## Контрольные измерения

Каждый эксперимент должен прогонять одну и ту же группу:

- `Performance.AsyncTextCounter` — plain text против async update;
- `Performance.Callbacks` — 20/30/50/100 transition, lifecycle и `Updated`;
- `Performance.FieldNotifyTextBinding` — async- и FieldNotify-путь;
- `Diagnostics.StorageLayout` — размеры структур;
- `Runtime.CallbackReentrancy` — удаление action/transition из callback;
- `Runtime.UpdateInterval` — throttling и обязательный final update.

Последний стабильный прогон Run 4 (UE 5.7, Mac arm64 Development):

| Сценарий | Результат |
| --- | ---: |
| 100 plain text counters | 11.341 μs/frame |
| 100 async text counters | 83.403 μs/frame |
| Async overhead | 72.061 μs/frame |
| 100 `Updated`, interval 0 | 75.941 μs/frame |
| 100 `Updated`, interval 0.033 | 43.802 μs/frame |
| 100 FieldNotify text bindings | 41.107 μs/frame |

После каждого изменения сравнивать нужно с прогоном той же Editor-сессии, а не только с историческими абсолютными значениями.

## Критерии принятия

- Все runtime-тесты проходят, включая `CallbackReentrancy` и `RemoveAtSwap`.
- Порядок `Started → Updated → Finished` не меняется.
- Async action безопасно переживает invalid widget и reentrant callback.
- Этап 1 не увеличивает runtime storage transition.
- Этап 4 принимается только при измеримом выигрыше памяти или dispatch, превышающем шум теста и усложнение lifetime-кода.
