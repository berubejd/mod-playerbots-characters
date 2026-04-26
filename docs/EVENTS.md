# Events

Here's the list of possible events that characters could react to.

- **Message received** — fires after the character receives a new message as a whisper or otherwise, for example "John tells you privately: How are you doing?" or "John says: It was a nice fight, huh?"
- **Party found item** — fires when any party member picks up a new item, only for rare (blue) items or higher tiers, for example "The party has found a legendary two-handed mace named Bane of the Damned" or "The party acquired an epic cloth robe named Robes of the Great Arcanist"
- **Character won duel** — fires after the character or someone else in the party wins the duel, for example "John has just won the duel against Joe"
- **Character leveled up** — fires after the character or someone else in the party reaches level 30 or higher, in a roleplay-friendly way, for example "John can feel their abilities growing stronger" (level-ups below 30 are ignored to avoid history spam)
- **Boss slain** — fires when any party member lands the killing blow on a significant opponent (dungeon/raid boss, world boss, or named elite), only when the group contains at least one real player; **always written to all character histories** regardless of whether anyone rolls to respond, for example: "The party has slain Kel'Thuzad (The Lich's Champion) in Naxxramas"
- **Quest taken** — fires when the **party leader** accepts a new quest from an NPC, game object, or item, only when the group contains at least one real player; a preliminary LLM call generates a one-line narrative summary of the quest, phrased naturally based on the source type (person, object, or item)
- **Quest completed** — fires when the **party leader** completes a quest, only when the group contains at least one real player; a preliminary LLM call generates a one-line narrative summary of the quest, phrased naturally based on the ender type (person or object)

When more than 5 minutes pass between consecutive history entries for a character, a narrator line `Narrator: *some time passes*` is automatically inserted to indicate the time gap. This line is not inserted if both the last and current messages are private (whisper) messages between the same characters.
