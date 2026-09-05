# Известные проблемы

[English version](../En/KnownIssues.md)

Подтверждённые проблемы runtime-логики. Это не roadmap: запись остаётся здесь до исправления и regression-теста.

## Transition update path

| Priority | Status | Problem | Current behavior | Intended resolution |
| --- | --- | --- | --- | --- |
| Critical | Solved | Inconsistent tick delta | `FWidgetTransitionSpring::Tick` получал полный `DeltaTime`, тогда как `FWidgetTransition::CurrentTime` ограничивался 50 ms. Во время hitch spring мог завершиться на секунду симуляции, когда transition продвинулся только на 50 ms. | `EffectiveDeltaTime` вычисляется один раз в `TickTransitions` и передаётся обоим проходам. Добавлен regression-тест `Runtime.TickDelta`. |
| Critical | Solved | Invalid easing row ends at `From` | При заданном, но неразрешённом `FCurveTableRowHandle`, easing alpha становился `0`, а transition всё равно достигал условия завершения. Последняя запись оставалась `FromValue`, а не `ToValue`. | При добавлении неразрешённый row очищается с warning; tick также интерпретирует отсутствующий cached curve как linear fallback. Добавлен regression-тест `Runtime.InvalidEasingFallback`. |
| Critical | Solved | Callback reentrancy invalidates transition reference | `Started`, `Updated` и `Finished` исполнялись, пока tick хранил ссылку на элемент `TArray`. Blueprint callback мог очистить, заменить или добавить transition, вызвав `RemoveAtSwap` или reallocation; последующий доступ к старой ссылке был небезопасен. | Callback исполняется из локальной копии delegate, а структурные удаления откладываются до завершения dispatch. После этого sidecar-индексы чинятся при `RemoveAtSwap`. Regression-тест `Runtime.CallbackReentrancy` покрывает очистку собственного transition из всех трёх callbacks и рост массива из `Updated`. |
| High | Solved | Явный `FromValue` не применялся до первого tick при нулевом Delay | `StartTransition` теперь сразу записывает явно заданный non-deferred From. Regression-тест `Runtime.ExplicitFrom` проверяет значение до первого tick, середину и завершение перехода. |
| High | Open | Deferred From и повторные циклы не показывают точное стартовое значение цикла один кадр | Выделить `ApplyCycleStartValue` для старта после delay и restart с учётом `bDeferFromValue`. |
| High | Open | Async action does not finish when start fails | `StartTransition` досрочно возвращает при invalid world/widget/binding/value, но async action уже зарегистрирован и никогда не получает lifecycle callback. | Пусть `StartTransition` возвращает success/ID; при неуспехе async action вызывает `Finished` и `SetReadyToDestroy`. |
| Medium | Solved | Spring callback progress is not simulation progress | Для spring без fit-to-time `NormalizedProgress` был основан на `Time`, хотя симуляция могла продолжаться после достижения `1.0`. | Progress удалён из публичного `Updated`: событие возвращает только фактический `Transition Value`. |

## Regression coverage to add

- Hitch: один tick с `DeltaTime > 1/20` не рассинхронизирует spring и `CurrentTime`.
- Missing CurveTable row: transition завершает на `ToValue` через linear fallback.
- Solved: callback clears its own transition during Started, Updated и Finished; `Updated` also forces transition-array reallocation.
- Solved: explicit From + zero delay применяется до первого tick (`Runtime.ExplicitFrom`).
- Deferred From, repeat with delay и YoYo cycle starts.
- Async transition with invalid binding finishes and освобождает action.
