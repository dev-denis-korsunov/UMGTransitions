# Spring

## Содержание

1. [Назначение](#назначение)
2. [Модель](#модель)
3. [Параметры](#параметры)
4. [Рекомендации](#рекомендации)
5. [Границы и развитие](#границы-и-развитие)
6. [Производительность и тестирование](#производительность-и-тестирование)

## Назначение

Spring создаёт инерционное движение для Float, Vector и Color. Все значения runtime представляет четырьмя float-каналами, поэтому один solver обслуживает все поддержанные типы.

Spring выбирайте, когда движение должно передавать вес, реакцию на жест или упругое появление. Для обычного state change сначала проверяйте easing: он проще контролируется и имеет фиксированную длительность.

## Модель

Solver использует аналитическое решение затухающего гармонического осциллятора с фиксированной массой `m = 1`:

```text
x'' + 2 × ζ × ω × x' + ω² × x = 0
```

`x` — смещение от target, `ω` — собственная частота, `ζ` — damping ratio. Для каждого канала есть собственные position и velocity, но общие параметры spring.

Under-damped и critically damped случаи рассчитываются отдельными аналитическими ветками. После состояния покоя значение snap-ается в target.

## Параметры

| Параметр | Смысл |
| --- | --- |
| `Spring Force` | Stiffness coefficient. `160` соответствует default response; большее значение ускоряет движение. |
| `Spring Damping` | Нормализованный UI-control `0..1`, отображаемый во внутренний damping ratio. |
| `Spring Max Speed` | Ограничивает длину velocity; `0` не ограничивает её. |
| `Fit To Time` | Выводит frequency из Time и Damping, чтобы достичь target к заданному дедлайну. |
| `End Tolerance` | Внутренняя относительная константа покоя; не увеличивает public Transition record. |

Подробнее о `Fit To Time`: [отдельная страница](FitToTime.md).

## Рекомендации

- Используйте умеренный damping для интерактивного отклика; повышайте его, если bounce отвлекает от текста или навигации.
- Не маскируйте неправильную duration экстремальным Force. Если нужна предсказуемая длительность, включите [Fit To Time](FitToTime.md).
- `Spring Max Speed` — художественный limiter. После ограничения trajectory уже не является точным аналитическим решением, поэтому задавайте его только для видимого UX-ограничения.
- Replace и Pipe сохраняют текущую spring velocity, чтобы новый target не создавал визуальный излом.

## Границы и развитие

- `Spring Damping` — UI-параметр, а не буквальная физическая единица.
- Overdamped режим, mass и независимые параметры по каналам намеренно не открыты: они расширят API сильнее, чем улучшат типичный UMG-сценарий.
- Приоритеты развития: Clamp Overshoot, project-level Reduce Motion, presets и затем external initial velocity для drag/scroll.

Полная история решений и отклонённых вариантов: [История оптимизаций](History/OptimizationHistory.md).

## Производительность и тестирование

Spring state хранится отдельно от linear transition, поэтому обычный transition не платит за position и velocity spring. Dense `TArray` storage выбран после сравнения с sparse вариантами.

- Корректность: `Runtime.Spring.Converges`, `Runtime.Spring.FitToTime`, `Runtime.TickDelta`.
- Измерения: `Performance.ModeMatrix`, `Performance.SpringTargetUpdate`, `Performance.PipeHandoff`.
- История storage и ParallelFor: [история оптимизаций](History/OptimizationHistory.md).
- Методика и актуальные ориентиры: [Тестирование](Testing.md).
