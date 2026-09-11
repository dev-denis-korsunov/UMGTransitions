# Composer

## Содержание

1. [Назначение](#назначение)
2. [Выбор дерева](#выбор-дерева)
3. [Порядок и волны](#порядок-и-волны)
4. [Рекомендации](#рекомендации)
5. [Производительность и тестирование](#производительность-и-тестирование)

## Назначение

`UWidgetSelectorLibrary` — Blueprint library для получения виджетов из иерархии UMG. Composer связывает выбор группы и её хореографию: сначала выбираются дети, затем их `WaveIndex` используется для delay или variation transition.

Traversal пересекает границу `WidgetTree → UUserWidget`, поэтому composed User Widget ведёт себя как продолжение общей иерархии.

## Выбор дерева

| Задача | Функция |
| --- | --- |
| Получить корень User Widget | `Get Widget Tree Root` |
| Найти именованный widget | `Find Widget by Name` |
| Получить прямых детей | `Get Widget Children` |
| Получить descendants | `Get Widget Descendants` |
| Ограничить уровень | `Get Widgets at Depth` / `Get Widgets through Depth` |
| Подняться к родителям | `Get Widget Parent` / `Get Widget Parents` |
| Выбрать descendants по именам | `Find Widget Descendants by Name(s)` |

`Get Widget Descendants` возвращает `FWidgetDescendant`: widget, depth, one-based wave index и нормализованное направление wave.

## Порядок и волны

Traversal может быть Depth First или Breadth First. Для sibling order доступны Left to Right, Right to Left и Center Out. Wave рассчитывается по geometry top-level children; deeper descendants наследуют wave родителя.

Wave patterns:

- Horizontal;
- Vertical;
- Manhattan;
- Radial.

Origin выбирается как угол, центр или конкретный widget. Для staggered enter-animation обычно достаточно `Delay = WaveIndex × Offset`.

## Рекомендации

- Сначала выберите визуальный порядок, потом pattern wave. Tree order не всегда совпадает с направлением, которое видит пользователь.
- Используйте короткий offset между волнами. Длинная последовательность делает интерфейс медленным.
- У крупных групп синхронизируйте второстепенные элементы и выделяйте stagger только для тех, куда нужно направить внимание.
- Не полагайтесь на geometry до layout pass: wave зависит от cached geometry.
- Для одинаковых экранов храните Composer + transition в макросе, передавая Root и pattern как явные параметры.

Правила hierarchy и staggering: [Рекомендации](Recommendations.md#хореография-и-иерархия).

## Производительность и тестирование

`WidgetSelector.Runtime.Hierarchy` проверяет traversal, depth, names, parent chain и переход через nested WidgetTree. `WidgetSelector.Diagnostics.GridWaveTranslation` и `NoDuplicateDescendants` защищают wave geometry и отсутствие повторов.

Composer — pure selection library: его стоимость не входит в tick transition. Запускайте selection при изменении структуры/появлении группы, а не каждый frame. Общая методика: [Тестирование](Testing.md).
