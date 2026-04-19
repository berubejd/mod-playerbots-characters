# Events

Here's the list of possible events that characters could react to.

- Message received - fires after the character receives a new message as a whisper or otherwise, for example "John tells you privately: How are you doing?" or "John says: It was a nice fight, huh?"
- Character got item - fires after the character or someone else in the party gets a new item, only fires for rare (blue) items or higher tiers, for example "John has just picked up a rare item, [Sulfuras, Hand of Ragnaros]"
- Character won duel - fires after the character or someone else in the party wins the duel, for example "John has just won the duel against Joe"
- Character leveled up - fires after the character or someone else in the party got a level up, for example "John has just leveled up and is now level 40"
- Character entered new area - fires after character enters new area, for example "You have just entered Brill in Tirisfal Glades"
- Character entered combat - fires when the **party leader** enters combat with an enemy, only when the group contains at least one real player, for example "Your party is entering combat with Murloc Raider"; history is only recorded if at least one bot rolls to respond (to avoid cluttering history with frequent silent combat entries)
- Quest completed - fires when the **party leader** completes a quest, only when the group contains at least one real player; a preliminary LLM call (using `PBC.QuestCompletionSystemPrompt` / `PBC.QuestCompletionUserPrompt`) generates a one-line narrative summary of the quest (e.g. "The party completed a task where they slew the Defias Brotherhood leader and recovered stolen goods."); each bot then independently rolls `PBC.ReplyChanceQuestCompletion` to either reply in party chat or record the summary silently in history