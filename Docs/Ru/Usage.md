# Использование

## Содержание

- [Быстрый старт](#быстрый-старт)
- [Паттерны и макросы](#паттерны-и-макросы)
- [Свойства и материалы](#свойства-и-материалы)
- [Счётчик](#счётчик)
- [AI + Unreal Engine MCP](#ai--unreal-engine-mcp)
  - [Подготовка](#подготовка)
  - [Подходящие задачи](#подходящие-задачи)
  - [Границы](#границы)
- [Производительность и тестирование](#производительность-и-тестирование)
  - [Стоимость счётчика](#стоимость-счётчика)
  - [Отклонённый общий update-event](#отклонённый-общий-update-event)
  - [Границы измерений](#границы-измерений)



## Быстрый старт

1. Создайте `Float`, `Vector` или `Color Transition Value` для target.
2. В `Create Widget Transition` укажите Widget, Widget Property, target value, Time и при необходимости Delay.
3. Передайте результат в `Add Widget Transition`.

Для стартового значения добавьте `From`. Для характера движения — `Easing` или `Spring`. Полный контракт этих узлов описан в [Transition](Transition.md).

## Паттерны и макросы

Макрос — предпочтительное место для повторяемого UX-паттерна: enter/exit, pop, fade, подчёркивание, staggered list. Он скрывает технические параметры, но оставляет на входе контекст: виджет, target, wave index и опциональный delay.

1. Соберите паттерн на одном виджете.
2. Оставьте только параметры, которые действительно меняются между местами.
3. Вынесите граф в макрос или Blueprint-функцию.
4. Для массива детей используйте [Composer](Composer.md), а не дублируйте узлы.

## Свойства и материалы

Widget Property picker предлагает поддержанные числовые свойства, slot properties и material parameters Image/Border. Часто используемые `RenderOpacity`, части `RenderTransform` и pivot имеют быстрые runtime adapters; прочие compatible float, vector и color свойства используют property-path binding.

Material parameter указывается как virtual property. Transition меняет scalar либо vector parameter на динамическом material instance, создаваемом для поддержанного виджета.

Подробнее о типах значений, binding и ограничениях: [Transition: свойства](Transition.md#свойства-и-binding).

## Счётчик

Для числа, выводимого в `Text`, разделяйте вычисление и представление:

1. Анимируйте `Float Transition Value`.
2. В `Updated` преобразуйте значение в форматированный текст или записывайте его в FieldNotify/ViewModel property.
3. Не обновляйте текст чаще, чем это заметно пользователю: настройте `Event Interval`.

Подробное сравнение direct callback, external binding, FieldNotify и ViewModel — в [Events: обновление текста](Events.md#обновление-текста).

## AI + Unreal Engine MCP

Unreal Engine MCP полезен не как замена дизайнера, а как быстрый цикл для воспроизводимой UI-работы:

```text
исследовать Widget Tree → выбрать свойства → изменить граф → compile → Automation → визуальная проверка
```

### Подготовка

- Давайте виджетам стабильные, смысловые имена.
- Выносите повторяемую анимацию в именованные макросы и Blueprint-функции.
- Храните варианты поведения в явных параметрах, а не в скрытых копиях графов.
- Фиксируйте ожидаемую хореографию: что появляется, куда направляется внимание, сколько длится и какие свойства меняются.

### Подходящие задачи

- найти детей виджета и применить общий transition;
- собрать stagger/wave на основе [Composer](Composer.md);
- заменить повторяющийся граф вызовом макроса;
- проверить доступные Widget Property и material parameters;
- собрать проект и запустить конкретную группу Automation.

### Границы

Агент не должен угадывать художественную хореографию, менять согласованный дизайн без явного задания или объявлять работу завершённой без compile/test-проверки. Реальные команды зависят от выбранного UE MCP-сервера.

## Производительность и тестирование

### Стоимость счётчика

Сравнивается форматирование числа и доставка в TextBlock. Значения — мкс/кадр для 100 элементов; UE 5.7.4 / Mac arm64 Development. Замеры 25–26.08.2026, 300 кадров. Это две отдельные пары сравнений, а не единый A/B/C/D.

| Серия | Вариант | Мкс/кадр | Что включено |
| --- | --- | ---: | --- |
| A/B, 25.08 | Прямой SetText | 11.002 | Число → FText::AsNumber → SetText |
| A/B, 25.08 | Async без Widget Property | 36.604 | Sampling + dynamic multicast + тот же SetText |
| C/D, 26.08 | Reflective CounterValue + TextDelegate | 30.956 | Запись числа + dynamic getter + форматирование |
| C/D, 26.08 | FieldNotify + native watcher | 20.956 | Запись числа + notification + форматирование |

- A/B: async добавляет 25.602 мкс/кадр, или 0.256 мкс/counter.
- C/D: push-вариант на 32.3% дешевле polling в этой серии.
- FieldNotify здесь измерен **каждый tick**; эти 20.956 нельзя выдавать за результат с Event Interval=0.033.
- Headless-замеры не включают полный Slate layout/paint; native watcher не равен скомпилированному MVVM Blueprint binding.

**Выбор:** direct SetText — для простого обновления числа; FieldNotify — для реактивного локального состояния; async — когда нужна дополнительная Blueprint-логика на Updated. ViewModel выбирается по владению данными: подтверждённого end-to-end MVVM числа в архиве нет. Для сокращения частоты увеличивайте Event Interval; [сравнение интервалов](Events.md#event-interval).

Подробнее: [методика счётчика](History/PerformanceTests.md#counter-update-baseline), [FieldNotify](History/PerformanceTests.md#fieldnotify-text-binding-baseline-per-tick).

### Отклонённый общий update-event

25.08.2026, те же 100 счётчиков: subsystem event + lookup по TransitionId — 35.844 против async 36.604 мкс/кадр. Разница −2.1% в одиночных прогонах недостаточна для вывода об ускорении. Вариант удалён: общий event сохраняет вызов receiver и SetText, добавляя поиск по id. [Подробности](History/PerformanceTests.md#отклонённые-эксперименты).

### Границы измерений

| Сценарий | Статус |
| --- | --- |
| Счётчик в plugin performance suite | Есть исторические числа выше |
| Material animation | Отдельного material benchmark в приведённой серии нет |
| Полный MVVM binding | Числовой baseline не записан; setter/notify не заменяет полный путь |
| Работа через MCP | Не измерена; команды и возможности зависят от подключённого сервера |
