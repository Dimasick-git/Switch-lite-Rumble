# docs/

Working design documents and the decision log for the Switch-lite-Rumble project.

| File | Что внутри |
| :--- | :--- |
| [`DESIGN-NOTES.md`](./DESIGN-NOTES.md) | **Главное:** сводка обсуждения и принятые решения — две архитектуры (слот vs внешний донгл), технические факты, что решили делать первым, открытые вопросы, участники |
| [`concept-external-approach.md`](./concept-external-approach.md) | Концепт «внешний/сисмодульный» путь (v1): перехват rumble в `hid` → форвардинг по USB-C/Bluetooth на внешний актуатор. Слот не задействован. Рекомендованный путь |
| [`slot-approach-technical-spec.md`](./slot-approach-technical-spec.md) | Техспека «через слот»: эмуляция картриджа, обход Lotus3, питание из слота. Отложен — упирается в обход защиты |

Связанные материалы в корне репозитория: [`../CHIPS.md`](../CHIPS.md) (выбор чипов и
физические ограничения) и [`../RESEARCH.md`](../RESEARCH.md) (архив источников и
находок по Lotus3 / протоколу картриджа).
