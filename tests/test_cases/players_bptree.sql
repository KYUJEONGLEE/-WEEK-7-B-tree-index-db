INSERT INTO players (nickname, game_win_count, game_loss_count) VALUES ('player_1', 10, 3);
INSERT INTO players (nickname, game_win_count, game_loss_count) VALUES ('player_2', 10, 5);
INSERT INTO players (nickname, game_win_count, game_loss_count) VALUES ('player_3', 20, 1);
SELECT nickname FROM players WHERE id = 2;
SELECT nickname FROM players WHERE game_win_count = 10;
SELECT nickname FROM players WHERE nickname = 'player_3';
DELETE FROM players WHERE nickname = 'player_2';
SELECT nickname FROM players WHERE id = 2;
