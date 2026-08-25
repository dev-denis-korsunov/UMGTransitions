# Known issues

Подтверждённые проблемы runtime-логики. Это не roadmap: запись остаётся здесь до исправления и regression-теста.

## Transition update path

| Priority | Problem | Current behavior | Intended resolution |
| --- | --- | --- | --- |
| Critical | Inconsistent tick delta | `FWidgetTransitionSpring::Tick` получает полный `DeltaTime`, тогда как `FWidgetTransition::CurrentTime` ограничивается 50 ms. Во время hitch spring может завершиться на секунду симуляции, когда transition продвинулся только на 50 ms. Это нарушает `FitSimulationToTime`. | Вычислять `EffectiveDeltaTime` один раз в `TickTransitions` и передавать его обоим проходам. |
| Critical | Invalid easing row ends at `From` | При заданном, но неразрешённом `FCurveTableRowHandle`, easing alpha становится `0`, а transition всё равно достигает условия завершения. Последняя запись остаётся `FromValue`, а не `ToValue`. | При добавлении переходить на linear fallback с warning либо отклонять transition. Предпочтителен fallback, гарантирующий достижение цели. |
| Critical | Callback reentrancy invalidates transition reference | `Started`, `Updated` и `Finished` исполняются, пока tick хранит ссылку на элемент `TArray`. Blueprint callback может очистить, заменить или добавить transition, вызвав `RemoveAtSwap` или reallocation; последующий доступ к старой ссылке небезопасен. | После каждого пользовательского callback не использовать сохранённую ссылку. Работать через `TransitionId` и повторно находить запись, либо завершать текущую итерацию. |
| High | `FromValue` is not applied at each cycle start | Без delay первый tick сразу вычисляет промежуточное значение. При `bDeferFromValue` From вообще не устанавливается в точке старта; при non-spring repeat с delay новая начальная величина также не применяется до первого update. | Выделить `ApplyCycleStartValue`: вызывать при старте, после delay и при restart с учётом `bDeferFromValue`. |
| High | Async action does not finish when start fails | `StartTransition` досрочно возвращает при invalid world/widget/binding/value, но async action уже зарегистрирован и никогда не получает lifecycle callback. | Пусть `StartTransition` возвращает success/ID; при неуспехе async action вызывает `Finished` и `SetReadyToDestroy`. |
| Medium | Spring callback progress is not simulation progress | Для spring без fit-to-time `NormalizedProgress` основан на `Time`, хотя симуляция может продолжаться после достижения `1.0`. | Документировать существующую семантику как time progress или ввести отдельный progress, определённый для spring. |

## Regression coverage to add

- Hitch: один tick с `DeltaTime > 1/20` не рассинхронизирует spring и `CurrentTime`.
- Missing CurveTable row: transition завершает на `ToValue` через linear fallback.
- Callback clears/replaces its own transition during Started, Updated и Finished.
- From + zero delay, deferred From, repeat with delay и YoYo.
- Async transition with invalid binding finishes and освобождает action.
